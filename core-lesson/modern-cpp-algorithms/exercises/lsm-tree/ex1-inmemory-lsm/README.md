# Exercise 1 — An In-Memory LSM-Tree, With Real Byte Formats

> **What you build:** `MemDB`, a complete LSM-tree that lives in RAM: an arena-backed skip-list
> memtable, immutable "sorted runs" encoded in the *exact* block format of an SSTable (Part 6 §5–7)
> but held in `std::string`s instead of files, Bloom filters, leveled compaction with snapshots, and
> amplification statistics.
>
> **Why in memory first?** It separates *algorithmic* bugs (ordering, merging, tombstones) from
> *storage* bugs (torn writes, fsync). When Exercise 2 swaps `std::string` for a file, the format code
> won't change — only where the bytes go. That's also how real engines are layered (`Env` abstraction
> in LevelDB/RocksDB `[ROCKS-OVERVIEW]`).

## Layout

```
ex1-inmemory-lsm/
├── CMakeLists.txt
├── include/lsm/
│   ├── todo.hpp            provided — Todo() helper
│   ├── status.hpp          provided — Status type
│   ├── coding.hpp          M1  fixed32/64, varint32/64, length-prefixed
│   ├── crc32c.hpp          M1  CRC32C + mask/unmask
│   ├── arena.hpp           M2  bump allocator with aligned allocation
│   ├── skiplist.hpp        M3  Pugh skip list, arena nodes, release/acquire publication
│   ├── internal_key.hpp    M4  user key ‖ fixed64(seq<<8|type), comparator
│   ├── memtable.hpp        M4  LevelDB-style entries in the arena
│   ├── block.hpp           M5  prefix-compressed block builder + reader (restart points)
│   ├── bloom.hpp           M6  deterministic Bloom filter (k stored in filter)
│   ├── sorted_run.hpp      M7  in-memory "SSTable": blocks + index + filter
│   ├── iterator.hpp        M8  iterator interface + k-way merging iterator
│   └── memdb.hpp           M8–M10 the LSM: levels, compaction, snapshots, stats
├── src/                    your .cpp files (empty stubs provided)
└── tests/                  provided test suite (the spec)
```

## Milestones

Each milestone: (1) read the listed lesson sections again, (2) write the code, (3) pass the tests
under ASan/UBSan, (4) add lines to `DECISIONS.md`, (5) *then* read the LevelDB file and compare.

### M1 — Encoding & checksums · `./build/tests coding crc`
Lessons: Part 2 §4–6, Part 3 §8. Compare with: `util/coding.{h,cc}`, `util/crc32c.h`.
- `PutFixed32/64`, `DecodeFixed32/64` — little-endian by **shifts**, no pointer casts (Part 2 §5).
- `PutVarint32/64`, `GetVarint32Ptr/64Ptr(p, limit, &v)` — must return `nullptr` on truncation *and*
  on over-long input (> 5 / > 10 bytes).
- `PutLengthPrefixed`, `GetLengthPrefixed` (consumes from a `std::string_view`).
- `crc32c::Value/Extend/Mask/Unmask`. Implement the table-driven, reflected CRC-32C (polynomial
  `0x82F63B78`). Test vectors in the tests are from RFC 3720 §B.4.

### M2 — Arena · `./build/tests arena`
Lessons: Part 2 §2, §7–8. Compare with: `util/arena.{h,cc}`.
- 4096-byte blocks; requests > 1/4 block get their own block; `AllocateAligned` aligns to
  `alignof(std::max_align_t)` (stricter than LevelDB's 8 — write down why you might want that).
- Non-copyable; destructor frees all blocks; `MemoryUsage()` is a relaxed atomic.
- Extra credit: make `Arena` satisfy `std::pmr::memory_resource` and use it with a `std::pmr::vector`.

### M3 — Skip list · `./build/tests skiplist`
Lessons: Part 3 §4, Part 2 §9 + §13. Compare with: `db/skiplist.h`.
- `p = 1/4`, `kMaxHeight = 12`, fixed seed. Nodes are placement-new'd into the arena with the
  trailing-array trick (Part 2 §9). No node is ever freed individually.
- Insert requires external synchronization (the test uses one writer). **Readers take no lock**:
  `Next()` = acquire load, publish with release store. The test `skiplist_concurrent_readers` runs 4
  readers against 1 writer; build with `-DLSM_TSAN=ON` and make it TSan-clean.
- Iterator: `Valid, key, Next, Prev, Seek, SeekToFirst, SeekToLast`.

### M4 — Internal keys & memtable · `./build/tests internal memtable`
Lessons: Part 5 §4. Compare with: `db/dbformat.{h,cc}`, `db/memtable.cc`.
- Internal key = user key ‖ `fixed64(seq << 8 | type)`; order: user key ascending, then **seq
  descending**.
- Memtable entries are single arena allocations encoded exactly like LevelDB's
  (`varint klen | key | tag | varint vlen | value`); the skip list stores `const char*`.
- `Get(user_key, snapshot_seq)` returns `kFound` / `kDeleted` / `kNotPresent`.

### M5 — Blocks · `./build/tests block`
Lessons: Part 6 §5–6. Compare with: `table/block_builder.cc`, `table/block.cc`.
- `BlockBuilder` must produce **byte-for-byte** the 27-byte example of Part 6 §5 (the test checks it).
- `BlockReader` validates the trailer (`num_restarts` fits, every restart offset in range) and returns
  `Status::Corruption` — never crashes — on garbage (the test fuzzes it).
- `Seek` = binary search over restart points, then linear scan.

### M6 — Bloom filter · `./build/tests bloom`
Lessons: Part 3 §7. Compare with: `util/bloom.cc`.
- Deterministic, *specified* hash (pick one, document it — FNV-1a 64-bit or MurmurHash-style; **not**
  `std::hash`, see `[CPPREF-hash]`). Double hashing; `k = clamp(floor(bits_per_key*0.69), 1, 30)`;
  minimum 64 bits; **k stored in the last byte**.
- Required: zero false negatives; FP < 2% at 10 bits/key on random 16-byte keys.
- Write in `DECISIONS.md` how your result compares with the formula `(1 − e^(−kn/m))^k`.

### M7 — Sorted runs (in-memory SSTables) · `./build/tests sorted_run`
Lessons: Part 6 §4–8. Compare with: `table/table_builder.cc`, `table/table.cc`.
- `SortedRunBuilder` takes internal keys in order and cuts a data block every `block_size` bytes,
  appends each block **with its 5-byte trailer** (type `0` + masked CRC) to one `std::string` "file",
  then a filter block, then an index block whose keys are **shortest separators** (implement
  `FindShortestSeparator` for the user-key part), then a 48-byte footer with your magic number.
- `SortedRun::Open(std::string bytes)` must parse it **from the footer backwards** exactly like a disk
  reader would. This is the code Exercise 2 reuses unchanged.

### M8 — Iterators & merging · `./build/tests merge`
Lessons: Part 3 §6, Part 5 §6. Compare with: `table/merger.cc`, `db/db_iter.cc`.
- `MergingIterator` over N child iterators (internal keys). Implement **both** a linear scan and a
  binary heap; benchmark crossover (Part 3 drill 3) and record it.
- `DBIterator` turns internal-key streams into user-visible keys at a snapshot: skip versions newer
  than the snapshot, hide shadowed versions, hide tombstoned keys.

### M9 — The LSM: flush, levels, compaction · `./build/tests memdb`
Lessons: Part 5 §5–8. Compare with: `db/db_impl.cc` (`DoCompactionWork`), `db/version_set.cc`.
- `Put/Delete/Write(batch)` assign sequence numbers; memtable → immutable → `SortedRun` in L0 when
  `memtable_bytes` is exceeded.
- L0 → L1 when L0 has `l0_compaction_trigger` runs; Ln → Ln+1 when `size(Ln) > level_base * T^(n-1)`.
- Implement LevelDB's two drop rules (Part 5 §7) **with snapshots**. The oracle test runs ~50k random
  Put/Delete/Flush/Compact/Snapshot operations against `std::map` copies — zero mismatches allowed.
- Versions are immutable `std::shared_ptr<const Version>`; readers pin the version they started with
  (Part 5 §10). Explain in `DECISIONS.md` why `shared_ptr` is acceptable here and where it costs you.

### M10 — Measure it · `./build/tests stats` + your own benchmark
Lessons: Part 5 §8.
- Count `user_bytes`, `flush_bytes`, `compaction_bytes_written`, `compaction_bytes_read`, runs per level,
  runs probed per `Get`.
- Experiment: insert 2M random 16-byte keys / 100-byte values with T ∈ {2, 4, 10, 20}. Plot write
  amplification and average runs probed per `Get`. Compare with `[LUO20]` Table 1 (Part 5 §8). Put
  the table and your explanation in `RESULTS.md`.

## Definition of done

- All tests green under ASan/UBSan; skip-list test green under TSan.
- `DECISIONS.md` has ≥ 1 sourced line per milestone + a "differences from LevelDB" list.
- `RESULTS.md` has the M10 experiment.
- You can explain, without notes, every byte of a block your code produced (`xxd` it).
