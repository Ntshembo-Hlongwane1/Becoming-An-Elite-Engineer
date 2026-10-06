# Part 6 — On-Disk Layout, Metadata, Headers, and "Page Ordering"

> This is the part that demystifies storage. We go byte by byte through every file an LSM-tree
> writes (LevelDB's formats, which RocksDB inherited), compute real bytes for small examples, and
> answer the question you said always worries you: **what "ordering" of pages actually matters,
> and why?**
>
> Prereqs: Part 1 (sectors, torn writes, page cache, fsync, rename), Part 2 (endianness, varints,
> padding, aliasing), Part 3 (sorted runs, Bloom, CRC).

Contents

1. Six different "orderings" — name them before you design anything
2. Design principles every format below follows
3. The WAL (log) format: 32 KiB blocks, fragmented records, CRCs — worked example
4. The SSTable format, top-down: footer → index → data
5. Data blocks byte-by-byte: prefix compression and restart points — worked example
6. Block trailers, BlockHandles, the index block and separator keys
7. Filter (Bloom) block and metaindex block
8. Footer and magic number; how a reader opens a table (syscall by syscall)
9. MANIFEST (VersionEdits) and CURRENT
10. Crash-safe write ordering: flush and compaction, step by step
11. Recovery, byte by byte
12. Pages vs blocks vs sectors: alignment choices
13. Summary + drills

---

## 1. Six orderings

"Page ordering" is not one thing. A storage engine juggles **six** orders, and most bugs come from
confusing them. **(derived — this taxonomy is mine; each row cites where the order is defined.)**

| # | Order | Rule | Defined by |
|---|---|---|---|
| O1 | **Key order inside a block** | strictly increasing internal key | "sorted order and partitioned into a sequence of data blocks" `[LDB-TABLE]` |
| O2 | **Block order inside an SSTable** | data blocks in key order, *then* meta blocks, metaindex, index, footer last | `[LDB-TABLE]` file diagram |
| O3 | **File order inside a level** | L≥1: by key range, non-overlapping; L0: by *recency* (file number) | `[LDB-IMPL]`, `NewestFirst` `[LDB-SRC:db/version_set.cc]` |
| O4 | **Level order** | smaller level number = newer data | `[LDB-IMPL]`, `[ROCKS-OVERVIEW]` "More recent data is stored in Level-0 (L0) and older data in higher-numbered levels" |
| O5 | **Record order in the WAL** | append order = commit order = sequence order | `[LDB-IMPL]`, `DBImpl::Write` |
| O6 | **Durability order** (the order bytes must *reach stable storage*) | "the thing" before "the pointer to the thing" | LevelDB `Sync()` comment `[LDB-SRC:util/env_posix.cc]`; `[LWN-DURABLE]` |

O1–O4 make **reads** correct and fast. O5 makes **recovery** replay history correctly. O6 makes
**crashes** survivable. Note O6 is *not* the order you call `write()` — the page cache and the disk
may reorder writes (Part 1 §6, §9). O6 is enforced only by `fsync` barriers between steps.

## 2. Design principles all formats below follow

**(derived from the formats themselves)**

1. **Explicit encoding**: every integer is `fixed32/64` little-endian or a varint; nothing is a raw
   struct (Part 2 §3–4).
2. **Length before data**: every variable-length field is length-prefixed; every length is checked
   against the bytes available.
3. **Checksum every unit you read independently**: each WAL record, each table block.
4. **Immutable once written**: "A block, once written to storage, is never modified."
   `[ROCKS-OVERVIEW]`. New state = new file.
5. **Self-description**: magic numbers identify the file type; the Bloom filter stores its own `k`;
   the MANIFEST records the comparator's name.
6. **Pointers are (offset, size) pairs**, never memory addresses — your algorithms book's own advice:
   "Avoid pointers for on-disk children; instead, store disk offsets or page IDs" `[ALGO §11.1.2]`.
7. **Atomic publish by rename**: the only "switch" is renaming `CURRENT`.

## 3. The WAL format `[LDB-LOG]`

### Structure

> "The log file contents are a sequence of 32KB blocks. The only exception is that the tail of the
> file may contain a partial block."

```
block := record* trailer?
record :=
  checksum: uint32     // crc32c of type and data[] ; little-endian
  length: uint16       // little-endian
  type: uint8          // One of FULL, FIRST, MIDDLE, LAST
  data: uint8[length]
```

So every record has a **7-byte header** (4 + 2 + 1). FULL=1, FIRST=2, MIDDLE=3, LAST=4.

> "A record never starts within the last six bytes of a block (since it won't fit). Any leftover bytes
> here form the trailer, which must consist entirely of zero bytes and must be skipped by readers."

A user record (e.g. one encoded `WriteBatch`, Part 5 §2) larger than the space left in the block is
split into FIRST / MIDDLE… / LAST fragments.

### Worked example (the doc's, with offsets computed **(derived)** by simulating the writer)

User records A = 1000 bytes, B = 97 270 bytes, C = 8000 bytes; block = 32 768 bytes:

```
A: FULL   block 0 offset 0      header 7 + 1000  -> ends 1007
B: FIRST  block 0 offset 1007   header 7 + 31754 -> ends 32768   (block 0 exactly full)
B: MIDDLE block 1 offset 0      header 7 + 32761 -> ends 32768
B: LAST   block 2 offset 0      header 7 + 32755 -> ends 32762
   trailer: 6 zero bytes at block 2 offset 32762               ("six bytes free … left empty as the trailer")
C: FULL   block 3 offset 0      header 7 + 8000  -> ends 8007
```

This matches `[LDB-LOG]`'s description exactly ("This will leave six bytes free in the third block,
which will be left empty as the trailer … C will be stored as a FULL record in the fourth block").

### Writer code `[LDB-SRC:db/log_writer.cc]` — read it with the example above

```cpp
Status Writer::AddRecord(const Slice& slice) {
  const char* ptr = slice.data();
  size_t left = slice.size();
  Status s;
  bool begin = true;
  do {
    const int leftover = kBlockSize - block_offset_;
    if (leftover < kHeaderSize) {
      // Switch to a new block
      if (leftover > 0) {
        // Fill the trailer (literal below relies on kHeaderSize being 7)
        static_assert(kHeaderSize == 7, "");
        dest_->Append(Slice("\x00\x00\x00\x00\x00\x00", leftover));
      }
      block_offset_ = 0;
    }
    const size_t avail = kBlockSize - block_offset_ - kHeaderSize;
    const size_t fragment_length = (left < avail) ? left : avail;
    RecordType type;
    const bool end = (left == fragment_length);
    if (begin && end)  type = kFullType;
    else if (begin)    type = kFirstType;
    else if (end)      type = kLastType;
    else               type = kMiddleType;
    s = EmitPhysicalRecord(type, ptr, fragment_length);
    ptr += fragment_length;  left -= fragment_length;  begin = false;
  } while (s.ok() && left > 0);
  return s;
}

Status Writer::EmitPhysicalRecord(RecordType t, const char* ptr, size_t length) {
  assert(length <= 0xffff);  // Must fit in two bytes
  char buf[kHeaderSize];
  buf[4] = static_cast<char>(length & 0xff);       // length, little-endian
  buf[5] = static_cast<char>(length >> 8);
  buf[6] = static_cast<char>(t);
  uint32_t crc = crc32c::Extend(type_crc_[t], ptr, length);   // CRC covers type + payload
  crc = crc32c::Mask(crc);  // Adjust for storage
  EncodeFixed32(buf, crc);
  Status s = dest_->Append(Slice(buf, kHeaderSize));
  if (s.ok()) {
    s = dest_->Append(Slice(ptr, length));
    if (s.ok()) s = dest_->Flush();
  }
  block_offset_ += kHeaderSize + length;
  return s;
}
```

Notice: the CRC covers the **type byte and payload** but not the length; the length is validated
indirectly (a wrong length makes the CRC check fail, or overruns the block — both caught by the
reader) **(derived)**.

### Why fixed 32 KiB blocks? (the doc's own reasons) `[LDB-LOG]`

1. "We do not need any heuristics for resyncing - just go to next block boundary and scan. If there
   is a corruption, skip to the next block."
2. "Splitting at approximate boundaries (e.g., for mapreduce) is simple: find the next block
   boundary and skip records until we hit a FULL or FIRST record."
3. "We do not need extra buffering for large records."

**(derived)** 32 KiB = 8 pages of 4 KiB. The record framing is *page-agnostic*; the block framing is
what gives a reader a **known resynchronisation point** every 32 KiB after corruption.

### Reader: the torn-tail rule `[LDB-SRC:db/log_reader.cc]`

```cpp
// Note that if buffer_ is non-empty, we have a truncated header at the
// end of the file, which can be caused by the writer crashing in the
// middle of writing the header. Instead of considering this an error,
// just report EOF.
...
if (kHeaderSize + length > buffer_.size()) {
  ...
  if (!eof_) { ReportCorruption(drop_size, "bad record length"); return kBadRecord; }
  // If the end of the file has been reached without reading |length| bytes
  // of payload, assume the writer died in the middle of writing the record.
  // Don't report a corruption.
  return kEof;
}
...
if (actual_crc != expected_crc) {
  // Drop the rest of the buffer since "length" itself may have
  // been corrupted and if we trust it, we could find some
  // fragment of a real log record that just happens to look
  // like a valid log record.
  ...
  ReportCorruption(drop_size, "checksum mismatch");
  return kBadRecord;
}
```

Two different situations, two policies **(derived)**:

- **Torn tail** (incomplete record at end of file): the expected result of a crash mid-append. That
  write was never acknowledged as durable (it hadn't finished), so dropping it is *correct*.
- **Bad CRC in the middle**: real corruption. Report it; don't trust the corrupt `length` field to
  find the next record — skip to the next block boundary.

## 4. The SSTable format, top-down `[LDB-TABLE]`

```
<beginning_of_file>
[data block 1]
[data block 2]
...
[data block N]
[meta block 1]          ← e.g. the Bloom "filter" block
...
[meta block K]
[metaindex block]       ← name → BlockHandle of each meta block
[index block]           ← one entry per data block
[Footer]                (fixed size; starts at file_size - sizeof(Footer))
<end_of_file>
```

"The file contains internal pointers. Each such pointer is called a BlockHandle and contains …
offset: varint64, size: varint64" `[LDB-TABLE]`.

**Why is all the metadata at the END? (derived)** The writer streams key/value pairs in sorted order
(O1) and emits a data block every ~4 KiB. It cannot know any block's offset until that block is
written, nor the index until all blocks are written. Putting the index and footer last lets the
whole file be produced in **one sequential pass** with **no seeks back** — exactly the I/O pattern
Part 1 says storage loves. Bigtable did the same: "A block index (stored at the end of the SSTable)"
`[BIGTABLE06 §4]`. The cost: a reader must start from the *end* — the fixed-size footer gives it a
known starting point.

Reading direction is therefore the opposite of writing direction **(derived)**:

```
WRITE:  data₁ → data₂ → … → dataₙ → filter → metaindex → index → footer      (one pass, append-only)
READ:   footer → index (+ metaindex → filter) → binary search → ONE dataᵢ   (3 small reads, then 1 per lookup)
```

## 5. Data blocks byte by byte

### Format `[LDB-SRC:table/block_builder.cc]` header comment

```
// When we store a key, we drop the prefix shared with the previous
// string.  This helps reduce the space requirement significantly.
// Furthermore, once every K keys, we do not apply the prefix
// compression and store the entire key.  We call this a "restart
// point".  The tail end of the block stores the offsets of all of the
// restart points, and can be used to do a binary search when looking
// for a particular key.  Values are stored as-is (without compression)
// immediately following the corresponding key.
//
// An entry for a particular key-value pair has the form:
//     shared_bytes: varint32
//     unshared_bytes: varint32
//     value_length: varint32
//     key_delta: char[unshared_bytes]
//     value: char[value_length]
// shared_bytes == 0 for restart points.
//
// The trailer of the block has the form:
//     restarts: uint32[num_restarts]
//     num_restarts: uint32
// restarts[i] contains the offset within the block of the ith restart point.
```

K = `block_restart_interval = 16` by default; target block size `block_size = 4 * 1024`
`[LDB-SRC:include/leveldb/options.h]`.

### Worked example **(derived — computed by a script implementing the rules above)**

Entries (user keys only, for clarity): `apple→1`, `applet→2`, `apply→3`.

| Entry | Offset | shared | unshared | value_len | key_delta | value | Bytes |
|---|---|---|---|---|---|---|---|
| apple | 0 | 0 | 5 | 1 | `apple` | `1` | `00 05 01 61 70 70 6c 65 31` |
| applet | 9 | 5 (`apple`) | 1 | 1 | `t` | `2` | `05 01 01 74 32` |
| apply | 14 | 4 (`appl`) | 1 | 1 | `y` | `3` | `04 01 01 79 33` |

Trailer: `restarts = [0]` → `00 00 00 00`; `num_restarts = 1` → `01 00 00 00`.

Whole block (27 bytes):

```
offset 00: 00 05 01 61 70 70 6c 65 31   05 01 01 74 32   04 01 01 79 33
offset 19: 00 00 00 00   01 00 00 00
```

Things to notice:

- `applet` costs 5 bytes instead of 9 — prefix compression **relies on O1** (sorted keys share
  prefixes with their neighbours). Unsorted keys would compress terribly **(derived)**.
- Each entry depends on the previous key, so you can't start decoding mid-block… except at a
  **restart point**, where `shared == 0`. Lookup = **binary search over the restart array**
  (fixed32, random access) to find the last restart ≤ target, then **linear scan** ≤ 16 entries
  **(derived from the comment above)**. That's the reason the restart array is fixed-width while
  entries use varints.
- The builder asserts O1 on every add: `assert(buffer_.empty() || options_->comparator->Compare(key,
  last_key_piece) > 0)` `[LDB-SRC:table/block_builder.cc]`.

## 6. Block trailer, BlockHandle, index block

### Block trailer `[LDB-SRC:table/table_builder.cc]`, `[LDB-SRC:table/format.h]`

```cpp
// File format contains a sequence of blocks where each block has:
//    block_data: uint8[n]
//    type: uint8
//    crc: uint32
static const size_t kBlockTrailerSize = 5;   // 1-byte type + 32-bit crc
```

```cpp
void TableBuilder::WriteRawBlock(const Slice& block_contents, CompressionType type, BlockHandle* handle) {
  handle->set_offset(r->offset);                 // remember where this block starts
  handle->set_size(block_contents.size());       // size EXCLUDES the 5-byte trailer
  r->status = r->file->Append(block_contents);
  if (r->status.ok()) {
    char trailer[kBlockTrailerSize];
    trailer[0] = type;                           // compression type (0 = none)
    uint32_t crc = crc32c::Value(block_contents.data(), block_contents.size());
    crc = crc32c::Extend(crc, trailer, 1);       // Extend crc to cover block type
    EncodeFixed32(trailer + 1, crc32c::Mask(crc));
    r->status = r->file->Append(Slice(trailer, kBlockTrailerSize));
    if (r->status.ok()) r->offset += block_contents.size() + kBlockTrailerSize;
  }
}
```

For our 27-byte block (uncompressed), the trailer is `00` + masked CRC32C → `00 6e 66 8d 18`
**(derived — computed with a CRC32C implementation checked against the standard test vector
`crc32c("123456789") = 0xE3069283`)**. The block occupies 32 bytes on disk; its BlockHandle is
`{offset, size=27}`.

### Reading a block `[LDB-SRC:table/format.cc]`

```cpp
size_t n = static_cast<size_t>(handle.size());
char* buf = new char[n + kBlockTrailerSize];
Status s = file->Read(handle.offset(), n + kBlockTrailerSize, &contents, buf);   // one pread
if (contents.size() != n + kBlockTrailerSize) { ... return Status::Corruption("truncated block read"); }
if (options.verify_checksums) {
  const uint32_t crc = crc32c::Unmask(DecodeFixed32(data + n + 1));
  const uint32_t actual = crc32c::Value(data, n + 1);
  if (actual != crc) { ... return Status::Corruption("block checksum mismatch"); }
}
switch (data[n]) { case kNoCompression: ... }
```

### Index block `[LDB-TABLE]`

> "This block contains one entry per data block, where the key is a string >= last key in that data
> block and before the first key in the successive data block. The value is the BlockHandle for the
> data block."

It is formatted like a data block (with restart interval 1 — every entry is a restart point:
`index_block_options.block_restart_interval = 1` `[LDB-SRC:table/table_builder.cc]`), so it can be
binary-searched directly.

Why "a string ≥ last key … and before the first key of the next" instead of just the last key? To make
the index **smaller**. The bytewise comparator's `FindShortestSeparator` `[LDB-SRC:util/comparator.cc]`:

```cpp
// Find length of common prefix
...
uint8_t diff_byte = static_cast<uint8_t>((*start)[diff_index]);
if (diff_byte < static_cast<uint8_t>(0xff) &&
    diff_byte + 1 < static_cast<uint8_t>(limit[diff_index])) {
  (*start)[diff_index]++;
  start->resize(diff_index + 1);
}
```

Example **(derived)**: last key of block i = `"the quick brown fox"`, first key of block i+1 =
`"the who"`. Common prefix `"the "`; next bytes `q` (0x71) vs `w` (0x77); 0x72 < 0x77 → separator
`"the r"`. Every key in block i is < `"the r"`, every key in block i+1 is ≥ it — 5 bytes instead of 19.

The index is built **lazily** for exactly that reason: the builder can't emit block i's index entry
until it sees the first key of block i+1 (`pending_index_entry` — "Invariant: r->pending_index_entry
is true only if data_block is empty" `[LDB-SRC:table/table_builder.cc]`).

## 7. Filter block and metaindex `[LDB-TABLE]`

- The metaindex "contains one entry for every other meta block where the key is the name of the meta
  block and the value is a BlockHandle pointing to that meta block". The Bloom filter's name is
  `"filter.<N>"` where N is the policy's name, e.g. `"filter.leveldb.BuiltinBloomFilter2"`
  `[LDB-SRC:util/bloom.cc]`. **(derived)** Looking meta blocks up *by name* means a reader that
  doesn't know a new meta block type simply never asks for it — forward compatibility for free.
- Filter block layout: "filter i contains the output of FilterPolicy::CreateFilter() on all keys that
  are stored in a block whose file offset falls within the range [ i*base ... (i+1)*base-1 ] …
  Currently, 'base' is 2KB."

```
[filter 0]
[filter 1]
...
[filter N-1]
[offset of filter 0]                  : 4 bytes
...
[offset of filter N-1]                : 4 bytes
[offset of beginning of offset array] : 4 bytes
lg(base)                              : 1 byte       (11, since 2^11 = 2048)
```

"The offset array at the end of the filter block allows efficient mapping from a data block offset to
the corresponding filter." So: index lookup gives the data block's offset → `offset >> 11` gives the
filter number → read 2 fixed32 offsets → test the key **(derived)**. Same "fixed-width array at the
end for random access" trick as the restart array.

RocksDB later switched to one "full filter" per file whose probes for a key stay within one CPU cache
line `[ROCKS-BLOOM]` (Part 3 §7).

## 8. Footer, magic number, and opening a table

### Footer `[LDB-TABLE]`, `[LDB-SRC:table/format.h]`, `[LDB-SRC:table/format.cc]`

```
metaindex_handle: char[p];     // Block handle for metaindex
index_handle:     char[q];     // Block handle for index
padding:          char[40-p-q];// zeroed bytes to make fixed length
                               // (40==2*BlockHandle::kMaxEncodedLength)
magic:            fixed64;     // == 0xdb4775248b80fb57 (little-endian)
```

- `kMaxEncodedLength = 10 + 10` (two varint64s, ≤ 10 bytes each), so footer =
  `2 * 20 + 8 = 48` bytes (`kEncodedLength`).
- Magic: "kTableMagicNumber was picked by running `echo http://code.google.com/p/leveldb/ | sha1sum`
  and taking the leading 64 bits."
- `Footer::DecodeFrom` rejects short input: `"not an sstable (footer too short)"`.

**Why a magic number? (derived)** It answers "is this even an SSTable?" in 8 bytes — catching a
truncated file, the wrong file, or a file of a different format/version before any pointer in it is
trusted. Why at the *end*? Because the footer is the entry point and is the **last** thing written: if
the writer crashed before finishing, the last 8 bytes won't be the magic → the file is rejected
instead of half-trusted.

### Opening a table and doing one lookup — syscall level **(derived)**

```
open("000124.ldb", O_RDONLY|O_CLOEXEC)                    → fd
fstat(fd) / known size from MANIFEST                       → file_size
pread(fd, buf, 48, file_size - 48)                         → footer; check magic
pread(fd, buf, index.size + 5, index.offset)               → index block; check CRC; keep in memory
pread(fd, buf, metaindex.size + 5, metaindex.offset)       → find "filter.…" handle
pread(fd, buf, filter.size + 5, filter.offset)             → filter block; keep in memory
--- per Get(key) ---
binary search index (in memory)        → BlockHandle of the only data block that could hold key
filter check (in memory)               → "definitely not here" ⇒ done, ZERO I/O
pread(fd, buf, h.size + 5, h.offset)   → data block; verify CRC
binary search restarts + scan ≤16 entries
```

RocksDB caches open fds in a **table cache** and blocks in a **block cache** ("LRU cache for blocks")
so the steady-state cost is usually zero or one `pread` `[ROCKS-OVERVIEW]`.

## 9. MANIFEST and CURRENT

### Why they exist

"File system operations are not atomic, and are susceptible to inconsistencies in the event of system
failure. Even with journaling turned on, file systems do not guarantee consistency on unclean restart.
POSIX file system does not support atomic batching of operations either. Hence, it is not possible to
rely on metadata embedded in RocksDB datastore files to reconstruct the last consistent state" —
"RocksDB has a built-in mechanism to overcome these limitations … by keeping a transactional log of
RocksDB state changes using Version Edit Records in the Manifest log files." `[ROCKS-MANIFEST]`

```
MANIFEST = { CURRENT, MANIFEST-<seq-no>* }
CURRENT = File pointer to the latest manifest log
MANIFEST-<seq no> = Contains snapshot of RocksDB state and subsequent modifications
version-edit      = Any RocksDB state change
version           = { version-edit* }
manifest-log-file = { version, version-edit* }
```
`[ROCKS-MANIFEST]`

### Format

The MANIFEST is "formatted as a log" `[LDB-IMPL]` — the *same* 32 KiB-block, CRC-per-record format as
the WAL (§3). Each record is one encoded `VersionEdit`, a sequence of **tag–value** fields
`[LDB-SRC:db/version_edit.cc]`:

```cpp
enum Tag {
  kComparator = 1,        // length-prefixed comparator name
  kLogNumber = 2,         // varint64: WAL files older than this are obsolete
  kNextFileNumber = 3,    // varint64: the file-number counter
  kLastSequence = 4,      // varint64: highest sequence number durably in SSTables
  kCompactPointer = 5,    // varint32 level + internal key (round-robin cursor, Part 5 §7)
  kDeletedFile = 6,       // varint32 level + varint64 file number
  kNewFile = 7,           // varint32 level + varint64 number + varint64 size + smallest + largest key
  // 8 was used for large value refs
  kPrevLogNumber = 9
};
```

and `EncodeTo` writes, e.g. for a new file:

```cpp
PutVarint32(dst, kNewFile);
PutVarint32(dst, new_files_[i].first);  // level
PutVarint64(dst, f.number);
PutVarint64(dst, f.file_size);
PutLengthPrefixedSlice(dst, f.smallest.Encode());
PutLengthPrefixedSlice(dst, f.largest.Encode());
```

Note `// 8 was used for large value refs`: **tag numbers are never reused** — an old file containing
tag 8 must never be misread as something new **(derived)**. Same discipline as `ValueType`'s "DO NOT
CHANGE THESE ENUM VALUES" (Part 5 §4).

The "current state" = replay all VersionEdits in the MANIFEST from the start. Each file's
`[smallest, largest]` key range in the MANIFEST is what lets a reader skip files without opening them
(Part 5 §6) **(derived)**.

### CURRENT

"CURRENT is a simple text file that contains the name of the latest MANIFEST file" `[LDB-IMPL]`, e.g.
the 16 bytes `MANIFEST-000005\n`. It is replaced with temp-file + fsync + **rename** (Part 1 §8,
`SetCurrentFile`). This rename is the **single atomic commit point** of the whole database's
structure **(derived)**.

## 10. Crash-safe write ordering (O6), step by step

### Flush of an immutable memtable to L0 **(derived from `[LDB-IMPL]` "Level 0" + the `Sync()` comment
`[LDB-SRC:util/env_posix.cc]` + Part 1 §8)**

| Step | Action | If we crash *after* this step and before the next… |
|---|---|---|
| 1 | write `000124.ldb` (data blocks … footer) | file is garbage/partial but **unreferenced**; WAL still has all data → recovery replays WAL; GC deletes the orphan |
| 2 | `fsync(000124.ldb)` | file durable but unreferenced → same as above |
| 3 | `fsync(dir)` so the new *name* is durable | same |
| 4 | append VersionEdit {NewFile 124 @L0, LogNumber=new WAL} to MANIFEST | the edit may be torn → reader sees a torn tail record and ignores it → still consistent |
| 5 | `fsync(MANIFEST)` | **commit point**: on recovery the MANIFEST says file 124 is live and old WAL is obsolete |
| 6 | delete old WAL; drop immutable memtable | if crash before delete: WAL is older than LogNumber → ignored and GC'd on recovery |

Break the order and see what fails **(derived)**:

- Swap 2 and 5 → MANIFEST durably references a file whose bytes aren't durable → after power loss,
  a "live" SSTable is truncated/garbage → **data loss** (magic/CRC check will at least *detect* it).
  This is precisely what LevelDB's comment warns about: "to avoid crashing in a state where the
  manifest refers to files that are not yet on disk".
- Do 6 before 5 → old WAL gone, new SSTable not committed → **acknowledged writes lost**.

### Compaction

Identical shape: write outputs → fsync → one VersionEdit {DeletedFile×inputs, NewFile×outputs} →
fsync MANIFEST → only then delete inputs (and only when no Version/iterator still references them,
Part 5 §10). Because a VersionEdit is **one log record**, adding outputs and removing inputs is
**atomic** — readers either see the old set of files or the new set, never a mix **(derived)**.

### Starting a new MANIFEST

`[ROCKS-MANIFEST]`: "When a manifest log file exceeds a certain size, a new manifest log file is
created with the snapshot of the RocksDB state. The latest manifest file pointer is updated and the
file system is synced. Upon successful update to CURRENT file, the redundant manifest logs are purged."
Order: write new MANIFEST (full snapshot) → fsync → write CURRENT.tmp → fsync → rename → fsync dir →
delete old MANIFEST.

> Compare LFS: it "keeps two CRs [checkpoint regions] … and writes to them alternately … it first
> writes out a header (with timestamp), then the body of the CR, and then finally one last block (also
> with a timestamp). If the system crashes during a CR update, LFS can detect this by seeing an
> inconsistent pair of timestamps." `[OSTEP-43]`. Same problem — atomically switching "the root" —
> solved without `rename`. You may want this technique for a raw block device (no filesystem) in your
> object store.

## 11. Recovery, byte by byte **(derived from `[LDB-IMPL]` Recovery + formats above)**

```
1. read CURRENT → "MANIFEST-000005"            (if missing: not a DB / DB being created)
2. open MANIFEST-000005; read log records (§3 reader):
     - CRC each record; a torn tail record = crash during append → stop there
     - decode VersionEdits; apply in order → set of live files per level, LogNumber,
       NextFileNumber, LastSequence, comparator name (must match ours!)
3. list directory; delete files that are neither live SSTables nor WALs ≥ LogNumber
     (orphans from crashed flushes/compactions)
4. for each WAL with number ≥ LogNumber, in file-number order (O5):
     read records → decode WriteBatches → re-insert into a fresh memtable
     (sequence numbers come from the batch header, so order is preserved)
     a torn tail = last write never acknowledged as synced → drop it
5. flush recovered memtable to a new L0 SSTable (or keep it), write a VersionEdit, new WAL
6. LastSequence = max(seen); open for business
```

Every SSTable is opened **lazily** ("We could open all sstables here, but it is probably better to be
lazy..." `[LDB-IMPL]`): its footer magic and block CRCs are checked when first read.

## 12. Pages vs blocks vs sectors — alignment choices

| Unit | Size | Who uses it |
|---|---|---|
| sector (atomic write unit) | 512 B | disk `[OSTEP-37]` |
| flash page / erase block | ~4 KiB / 128–256 KiB | SSD `[OSTEP-44]` |
| OS page (page cache) | 4096 B **(measured)** | kernel `[KERNEL-MM]` |
| WAL block | 32 768 B | LevelDB `[LDB-LOG]` |
| SSTable data block | ~4096 B *before* compression, not aligned | LevelDB `block_size` `[LDB-SRC:include/leveldb/options.h]` |
| Bigtable SSTable block | "typically … 64KB" | `[BIGTABLE06 §4]` |

LevelDB's `block_size` is a *target for uncompressed data*: "The actual size of the unit read from
disk may be smaller if compression is enabled" `[LDB-SRC:include/leveldb/options.h]`. Blocks are
packed back-to-back, so they **straddle 4 KiB page boundaries** **(derived)**. With buffered I/O this
is fine — the page cache reads whole pages, and a block spanning 2 pages costs 2 page reads at most.
With `O_DIRECT`, offsets and lengths may need alignment `[MAN-open]` — an engine would then pad blocks
to the alignment or read the enclosing aligned range. **Trade-off (derived):** padding wastes space;
straddling can cost an extra page read per lookup. You'll measure this in Exercise 2.

Rule of thumb from all of the above: **choose your unit to match the slowest boundary you cross**.
Point reads want small blocks (read less); scans and compression want big blocks (fewer seeks, better
ratio) — which is why Bigtable used 64 KB and LevelDB 4 KB.

## 13. Summary

| File | Header / framing | Integrity | Ordering it relies on | Commit mechanism |
|---|---|---|---|---|
| WAL | 32 KiB blocks; 7-byte record header (crc, len, type) | CRC32C per record (masked) | O5 append order | fsync (if sync=true) |
| SSTable data block | entries (varint shared/unshared/vlen) + restart array + count | 5-byte trailer: type + masked CRC | O1 sorted keys | immutable after write |
| SSTable index / metaindex / filter | block format / named handles / offset array + lg(base) | same trailer | O2 (written after data) | — |
| SSTable footer | 2 BlockHandles + padding to 40 + 8-byte magic | magic number | last thing written | fsync before MANIFEST |
| MANIFEST | log format; VersionEdit tag–value records | CRC per record | edits replayed in order | fsync |
| CURRENT | one line of text | — | — | **rename** (atomic) + fsync dir |

## Drills

1. With a hex editor or `xxd`, hand-decode the 27-byte block from §5. Then change `applet`'s value to
   `22` and recompute the bytes (CRC with any CRC32C tool).
2. Simulate the WAL writer for records of sizes 32 761, 0, 1, 32 762. Where do FIRST/LAST fragments
   and trailers appear? Is a zero-length record emitted?
3. For each row of §10's flush table, write the exact recovery action LevelDB takes. Then design
   the same table for your object store's "upload complete" operation.
4. Compute `FindShortestSeparator("abc", "abd")` and `FindShortestSeparator("abc1", "abcd")` by hand.
   One shortens and one doesn't — which, and why? (Hint: read the condition
   `diff_byte + 1 < limit[diff_index]` and look up the ASCII codes.)
5. Why must `kLastSequence` be recorded in the MANIFEST even though WAL records contain sequence
   numbers? (Hint: what if every WAL has been deleted?)

## My summary

_(write 5 lines here)_
