# Part 7 — The LSM-Tree Through a Security Researcher's Eyes

> You will build an LSM-tree in the exercises, and later red-team your own object store black-box.
> This part gives you the *map of where bugs live* in this class of software — derived from the
> formats and code in Parts 5–6 — using the three-hats method: **(1) how you'd discover it,
> (2) what it's worth to an attacker (and its ceiling), (3) what it costs the defender**.
>
> Everything here is a *bug class* reasoned from sourced mechanics, not a claim about a specific CVE.
> When you research real CVEs later, use these classes as search terms.

Contents

1. The attack surface of a storage engine
2. Class A — Parsers trusting on-disk lengths and offsets
3. Class B — Checksums are not authentication
4. Class C — Uninitialized bytes written to disk (information leak)
5. Class D — Tombstone resurrection (deleted data comes back)
6. Class E — Data remanence (deleted data is still on disk)
7. Class F — Write stalls and compaction debt as denial of service
8. Class G — Durability lies: fsync error handling
9. Class H — Lifetime bugs: views and iterators outliving their owners
10. A checklist for when you red-team your own engine

---

## 1. Attack surface

**(derived)** Inputs an engine consumes:

| Input | Who controls it in a real deployment |
|---|---|
| keys and values via the API | any client (in your object store: HTTP uploaders) |
| bytes of WAL / SSTable / MANIFEST / CURRENT on recovery and on read | whoever can write the data directory (another tenant on a shared disk, a restored backup, a malicious "import", a corrupted disk) |
| file *names* in the directory | same |
| timing / volume of writes | any client |

The second row is the interesting one: **every file format in Part 6 is a parser of untrusted bytes**
the moment files can come from outside — backups, replication, copying a data directory between
machines, or tooling that operates on files directly (RocksDB exposes file-level APIs such as
`GetLiveFilesMetaData`, `CompactFiles` and `DeleteFile` `[ROCKS-OVERVIEW]`, and ships `sst_dump`/`ldb`
tools that parse SST and MANIFEST files `[ROCKS-OVERVIEW]` §5). Treat any file you didn't just write
yourself as untrusted.

## 2. Class A — trusting on-disk lengths and offsets

**Mechanics.** Varint lengths (Part 2 §6), `shared/unshared/value_length` in blocks, BlockHandle
`{offset,size}`, restart offsets, filter offsets, `num_restarts` — each is a *claim* by the file.

**1 — Discovery.** Fuzz the decoder: feed random/mutated blocks to `Block::Seek`/your block reader
under ASan+UBSan (your algorithms book recommends exactly these tools for correctness work
`[ALGO §3.3, §25.1]`, and fuzzing inputs in `[ALGO §29.1]`). Mutate one field at a time: a huge
`num_restarts`, a restart offset past the end, `shared` larger than the previous key's length,
a varint with 6+ continuation bytes, a BlockHandle pointing past EOF.

**What correct code looks like** — LevelDB's varint decoder takes a `limit` and caps the loop at 5
bytes (Part 2 §6); `ReadBlock` checks "truncated block read" and the CRC before interpreting
anything `[LDB-SRC:table/format.cc]`; `Footer::DecodeFrom` rejects short input and checks the magic.

**2 — Value.** An out-of-bounds *read* in a parser = crash (DoS) or memory disclosure (adjacent heap
returned as a "value"). An out-of-bounds *write* while decoding (e.g. copying `unshared` bytes into a
fixed key buffer sized by an unchecked `shared`) = heap corruption → the classic heap-overflow
pathway your memory book describes: "Heap overflows can modify data in other heap-allocated areas or
corrupt memory management structures, potentially allowing attackers to execute arbitrary code."
`[MEM §5.1]`. Ceiling: depends on write primitive; honest default is DoS.

**3 — Defense.** Make every length check *by construction*: decode into `std::span<const std::byte>`
and only advance through a cursor type whose `take(n)` fails if `n > remaining()`. Note a correction
to `[MEM §5.1]`'s prevention advice ("Use container classes like std::vector or std::array, which handle
boundary checks automatically"): `std::vector::operator[]` performs "No bounds checking … unless the
implementation is hardened (since C++26)" `[CPPREF-vector-at]` — only `.at()` checks. Containers
help with *lifetime*, not automatically with *bounds*.

## 3. Class B — checksums are not authentication

**Mechanics.** CRC32C detects accidents (Part 1 §10, `[OSTEP-45]`). The algorithm is public and
keyless, so anyone who can modify a block can recompute a valid CRC **(derived)**. The mask trick
(Part 3 §8) is not a secret either — it's in the source.

**1 — Discovery.** Edit a value inside an SSTable, recompute the trailer CRC (Part 6 §6 gives you
the exact recipe), and see whether the engine serves your bytes. It will.

**2 — Value.** Integrity bypass: if the storage directory is reachable (shared volume, backup
restore, "import SST" feature), an attacker can change stored values undetected. Combined with
Class A, a CRC-valid malicious block reaches the parser — the CRC gate does not protect the parser.

**3 — Defense / business.** If files cross a trust boundary, use a keyed MAC (e.g. HMAC-SHA-256) or
authenticated encryption per block/file; keep CRCs for fast accident detection. Business: silent
tampering with stored objects is a data-integrity and compliance failure (customers' data changed
without trace).

## 4. Class C — uninitialized bytes on disk

**Mechanics.** Part 2 §3 measured 5 padding bytes in `[ALGO §11.2.2]`'s `DiskNode`; writing the raw
struct ships whatever was in those bytes **(measured + derived)**. The same happens if you allocate a
4 KiB block buffer, fill 3 900 bytes, and write all 4 096.

**1 — Discovery.** Write a file, `xxd` it, look for bytes you didn't put there (stack/heap
fragments, pieces of other users' keys). Run under MSan (Clang's MemorySanitizer) to flag
uninitialized bytes reaching `write()`.

**2 — Value.** Information disclosure across tenants or into backups. Ceiling: whatever secrets were
recently in that memory.

**3 — Defense.** Explicit encoding (no raw structs), zero-initialise buffers you write
(LevelDB zero-fills the WAL trailer and the footer padding explicitly — `Slice("\x00\x00\x00…")`,
"padding: zeroed bytes" `[LDB-LOG]`, `[LDB-TABLE]`).

## 5. Class D — tombstone resurrection

**Mechanics.** Part 5 §7: a tombstone may be dropped only if no older level could contain the key
(`IsBaseLevelForKey`) and no snapshot needs it. Drop it early and the older value reappears.
Similar hazards: L0 files probed in the wrong order (must be newest-first, `NewestFirst`); sequence
numbers that go backwards after recovery (if `kLastSequence` isn't recovered — Part 6 drill 5).

**1 — Discovery.** Differential testing: run the same random sequence of Put/Delete/flush/compact/
reopen against your engine and against `std::map` (an *oracle*). Any key the oracle says is deleted
but your engine returns = resurrection. This is the property-based testing your book describes in
`[ALGO §29.1]`, and it's how RocksDB validates correctness at scale ("The db_stress test is used to
validate data correctness at scale" `[ROCKS-OVERVIEW]`).

**2 — Value.** Deleted credentials/tokens/objects become readable again; authorization checks based
on "object deleted" are bypassed. Ceiling: data exposure, policy bypass.

**3 — Defense / business.** "Right to erasure" requests that silently un-erase are a compliance
incident. Make the invariant testable: keep the oracle test in CI forever.

## 6. Class E — data remanence

**Mechanics.** A Delete writes a tombstone; the old value physically remains in older SSTables until a
compaction rewrites them (and even then, the freed disk blocks aren't wiped). Bigtable says it
plainly: major compactions "allow Bigtable to reclaim resources used by deleted data, and also allow
it to ensure that deleted data disappears from the system in a timely fashion, which is important for
services that store sensitive data." `[BIGTABLE06 §5.4]`. Below the LSM, SSD FTLs keep old page versions
until garbage collection `[OSTEP-44]`.

**1 — Discovery.** Put a secret, Delete it, then `strings` the data directory / raw device.

**2 — Value.** Anyone with later access to the disk image (stolen disk, backup, forensics) recovers
"deleted" data.

**3 — Defense.** Bound the time-to-physical-deletion (periodic full compactions over ranges with
tombstones), encrypt data at rest with per-object keys so deletion = destroying the key ("crypto
shredding" — research this term). Business: retention-policy and privacy-law exposure.

## 7. Class F — write stalls / compaction debt as DoS

**Mechanics.** Part 5 §9: writes create compaction debt; at 8 L0 files LevelDB slows writers by 1 ms,
at 12 it blocks them `[LDB-SRC:db/dbformat.h]`. Write amplification multiplies a client's bytes by
~T·L (Part 5 §8).

**1 — Discovery.** Black-box: drive writes with keys spread across the whole key space (maximises
overlap → maximal compaction work) and watch p99 latency of *other* tenants. Large values are
especially effective per request.

**2 — Value.** Availability attack with leverage: 1 byte written by the attacker costs the server
tens of bytes of I/O **(derived from the WA estimate)**. Ceiling: DoS, not code execution.

**3 — Defense / business.** Per-tenant rate limits on *bytes*, not requests; admission control tied
to compaction debt; separate flush threads (RocksDB's advice `[ROCKS-OVERVIEW]`). Business: latency
SLO breaches for every customer sharing the node.

## 8. Class G — durability lies

**Mechanics.** Part 1 §9: fsync errors may be reported only once; retrying can "falsely report
success" `[PG-FSYNC]`. LevelDB's response is to enter a permanent error mode: "we force the DB into a
mode where all future writes fail" `[LDB-SRC:db/db_impl.cc]`.

**1 — Discovery.** Use a fault-injecting filesystem or block device (e.g. Linux `dm-flakey` /
`dm-error` — look these up) to make writeback fail; check whether your engine still acknowledges
writes as durable.

**2 — Value.** Mostly reliability, but a security angle exists: an attacker who can fill the disk
(ENOSPC) at the right moment may cause acknowledged writes (e.g. an audit log entry) to disappear.

**3 — Defense.** Fail-stop on sync error; never retry-and-forget; recovery from the WAL is the only
trustworthy path.

## 9. Class H — lifetime bugs

**Mechanics.** `Slice`s into the arena and into block buffers (Part 2 §11); Versions and memtables are
reference counted (Part 5 §10). Drop a reference too early and an iterator reads freed memory.

**1 — Discovery.** ASan + a stress test that iterates while flushing/compacting concurrently; TSan for
races on the version list.

**2 — Value.** Use-after-free in a long-running server process is a classic exploitation primitive
(heap grooming → type confusion). Ceiling: potentially code execution; realistically crash/info-leak
first.

**3 — Defense.** RAII handles for refs (an `Unref` in a destructor, never manual), and make views
non-escaping in APIs (return owning copies across thread boundaries).

## 10. Checklist for red-teaming your own engine later

- [ ] Fuzz every decoder (varint, block, footer, VersionEdit, WAL record) with ASan/UBSan.
- [ ] Flip one byte at every offset of a small SSTable; the engine must return *Corruption*, never crash.
- [ ] Recompute CRCs after tampering; confirm you understand that the engine *accepts* it.
- [ ] `xxd` your files: any byte you didn't intend?
- [ ] Oracle-test Put/Delete/flush/compact/reopen sequences against `std::map`; zero resurrections.
- [ ] Kill -9 at random points during flush/compaction 1000×; every reopen must succeed and lose
      nothing acknowledged with `sync=true`.
- [ ] Write a "compaction-debt" load generator; measure other clients' p99.
- [ ] Put a secret, delete it, search the raw files.

## My summary

_(write 5 lines here)_
