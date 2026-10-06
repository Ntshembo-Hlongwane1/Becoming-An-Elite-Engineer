# DLSM On-Disk Format, version 1

> This is the **specification** your Exercise 2 engine must implement. Writing a format spec *before*
> code is what storage engineers do: the spec is the contract between the writer you write today, the
> reader you write tomorrow, the recovery code that runs after a crash, the `lsmdump` tool, and — later
> — you as a red-teamer reading these files black-box.
>
> It is deliberately close to LevelDB (notes Part 6) so you can compare byte-for-byte, with **one
> addition LevelDB doesn't have: a file header** on every table, and **one option LevelDB doesn't
> have: page-aligned blocks**. Both exist to teach "metadata/headers" and "page ordering".

All integers are **little-endian**. `varint32/64` = Protocol Buffers base-128 varints `[PB-ENC]`.
`lp(x)` = length-prefixed string = `varint32 len ‖ bytes`. `crc` = CRC-32C; `mcrc` = masked CRC
(`rotr15(crc) + 0xa282ead8`). Rationale for every rule is in notes Part 6 (§ numbers in brackets).

## 1. Directory layout

```
<dbdir>/
  LOCK               empty file; held with flock(LOCK_EX|LOCK_NB) while open      [only one process]
  CURRENT            "MANIFEST-<6 digits>\n"; replaced ONLY by temp+fsync+rename+fsync(dir)  [P6 §9]
  MANIFEST-000001    log-format file of VersionEdits                               [P6 §9]
  000002.log         write-ahead log (WAL)                                         [P6 §3]
  000003.sst         sorted table
  000004.dbtmp       temp file (only during CURRENT replacement)
```

All numbered files share **one** counter, `next_file_number` (persisted in the MANIFEST). Numbers are
never reused. Bigger number = newer file.

## 2. Log format (WAL and MANIFEST) — identical to LevelDB `[LDB-LOG]`

```
file   := block* partial_block?
block  := record* trailer?           (block = 32768 bytes)
record := mcrc: fixed32   -- crc of (type byte ‖ data)
          length: fixed16
          type: uint8     -- 1 FULL, 2 FIRST, 3 MIDDLE, 4 LAST
          data: uint8[length]
trailer:= 0..6 zero bytes (a record never starts in the last 6 bytes of a block)
```

Reader rules:

| Situation | Action |
|---|---|
| incomplete header or payload **at end of file** | treat as EOF (torn tail from a crash) — no corruption report |
| crc mismatch, or length runs past the block, **not at end of file** | report corruption (bytes dropped), skip to next block boundary |
| MIDDLE/LAST without FIRST, FIRST without LAST before another FIRST/FULL | report corruption, drop the partial user record |
| unknown type | report corruption, skip record |

## 3. WAL payload = one WriteBatch per user record

```
batch  := sequence: fixed64      -- sequence number of the FIRST op; op i gets sequence+i
          count: fixed32
          op[count]
op     := type: uint8 (1 = PUT, 0 = DELETE)   lp(key)   [lp(value) if PUT]
```

(Same as LevelDB's `WriteBatch::rep_` `[LDB-SRC:db/write_batch.cc]`.)

## 4. Sorted table (`.sst`)

```
offset 0                        HEADER (64 bytes)                                [NEW vs LevelDB]
[padding to block_align]        only if flags.ALIGNED
DATA BLOCK 0 ‖ trailer(5)
[padding to block_align]        only if flags.ALIGNED, before EVERY data block
DATA BLOCK 1 ‖ trailer(5)
...
FILTER BLOCK ‖ trailer(5)       (absent if bloom_bits_per_key == 0)
METAINDEX BLOCK ‖ trailer(5)
INDEX BLOCK ‖ trailer(5)
FOOTER (48 bytes)               ends exactly at end of file
```

### 4.1 Header (64 bytes, written FIRST)

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 8 | magic | ASCII `DLSMTBL1` (bytes `44 4c 53 4d 54 42 4c 31`) |
| 8 | 4 | format_version | `1` |
| 12 | 4 | flags | bit 0 = ALIGNED (data blocks start at multiples of `block_align`) |
| 16 | 4 | block_align | `0` if not aligned, else a power of two ≥ 512 (e.g. 4096) |
| 20 | 4 | target_block_size | the builder's target (informational) |
| 24 | 8 | file_number | must equal the number in the file name |
| 32 | 28 | reserved | all zero |
| 60 | 4 | header_mcrc | masked crc of bytes [0, 60) |

**Why a header when the footer already exists?** (read notes Part 6 §4 and §8 first)
- The header holds what a reader must know *before* interpreting anything else (version, flags), and
  what identifies the file from its *first* bytes (tools, `file(1)`, a human with `xxd`).
- It is written **first**, so it may contain only facts known before any data is written. It can
  **never** contain offsets of later blocks — those live in the footer, written **last**. Rewriting the
  header at the end would break append-only writing (notes Part 6 §4, design principle 4).
- `file_number` in the header catches a file that was renamed/copied to the wrong name — a mild form
  of OSTEP's "physical identity" defence against misdirected writes `[OSTEP-45]`.

### 4.2 Blocks and trailers

Exactly Exercise 1's formats (`block.hpp`, `format.hpp`):
- data/index/metaindex block contents: notes Part 6 §5; index uses restart interval 1 and
  shortest-separator keys; data blocks use internal keys.
- trailer: `type: uint8 = 0` ‖ `mcrc(contents ‖ type): fixed32`.
- BlockHandle = `varint64 offset ‖ varint64 size` (size excludes the trailer).
- metaindex: one entry `"filter.dlsm.bloom"` → BlockHandle of the filter block (if present).
- filter block: one Bloom filter (Exercise 1 M6 format) over all **user** keys of the table.

### 4.3 Footer (48 bytes, written LAST)

`metaindex_handle ‖ index_handle ‖ zero padding to 40 bytes ‖ fixed64 magic` where magic is the bytes
`MINILSM1` (Exercise 1's `kTableMagic`).

### 4.4 Alignment ("page ordering") rules

- If `ALIGNED`: before each **data** block, pad with zero bytes until `offset % block_align == 0`.
  Meta/index blocks and the footer are packed (they're read once at open and cached).
- In ALIGNED mode the builder must **cut the current data block before adding an entry** if the
  block is non-empty and adding the entry could make `block + 5-byte trailer` exceed `block_align`
  (estimate the entry conservatively: key + value + 15 bytes of varints + 4 for a possible restart).
  A block holding a single entry larger than a page is allowed to exceed it.
  (In packed mode the LevelDB rule applies: cut *after* the estimate reaches `block_size`.)
- Readers never *assume* alignment; they always follow BlockHandles. Alignment is a performance
  property, not a correctness one.

## 5. MANIFEST payload = one VersionEdit per log record (tag numbers match LevelDB)

```
edit  := field*
field := varint32 tag, then:
   1 comparator        lp(name)                     -- must equal "dlsm.InternalKeyComparator.v1"
   2 log_number        varint64                     -- WALs with number < this are obsolete
   3 next_file_number  varint64
   4 last_sequence     varint64
   5 compact_pointer   varint32 level, lp(internal key)
   6 deleted_file      varint32 level, varint64 number
   7 new_file          varint32 level, varint64 number, varint64 size, lp(smallest ikey), lp(largest ikey)
```

Unknown tag ⇒ corruption (refuse to open; never guess). A new MANIFEST starts with one edit holding
the full current state (comparator, log_number, next_file_number, last_sequence, every live file).

## 6. Durability protocol (the order is part of the format)

**Commit a write (sync=true):** append batch to WAL → `fdatasync(wal)` → apply to memtable →
acknowledge.

**Flush (memtable → table N):**
1. write `N.sst` completely; `fsync(N.sst)`
2. `fsync(dir)` (makes the new name durable)
3. append edit {new_file N @L0, log_number = current WAL number} to MANIFEST; `fsync(MANIFEST)`
   ← **commit point**
4. delete obsolete WAL(s)

**Compaction:** write outputs, fsync each, fsync(dir) → one edit {deleted inputs, new outputs,
compact_pointer} → fsync(MANIFEST) ← commit → delete inputs when no reader pins them.

**New MANIFEST:** write `MANIFEST-M` with a full snapshot edit → fsync → write `M.dbtmp` containing
`"MANIFEST-M\n"` → fsync → `rename(M.dbtmp, CURRENT)` → fsync(dir) → delete old MANIFEST.

**Any fsync failure:** the DB enters a permanent error state; all later writes fail (notes Part 1 §9,
Part 5 §2 step 8).

## 7. Recovery (open)

1. acquire `LOCK` (fail if held)
2. read `CURRENT` (must end in `\n`, name must match `MANIFEST-\d{6}`)
3. replay MANIFEST edits in order; torn tail = stop; corruption in the middle = refuse to open
4. comparator name must match
5. **before writing any new file**: set `next_file_number = max(next_file_number, 1 + every number
   found in the directory)`, then delete files that are not: CURRENT, LOCK, the live MANIFEST, live
   tables, WALs with number ≥ log_number. (A crash after creating `000111.sst` but before the MANIFEST
   commit leaves an orphan whose number the MANIFEST still thinks is free — reuse it with `O_EXCL` and
   recovery fails; reuse it without `O_EXCL` and you may clobber something. The crash test finds this.)
6. replay WALs with number ≥ log_number in ascending order into a memtable (sequence numbers from the
   batches); last_sequence = max(last_sequence, highest replayed)
7. flush that memtable to L0 (protocol §6), start a new WAL, write a fresh MANIFEST + CURRENT
