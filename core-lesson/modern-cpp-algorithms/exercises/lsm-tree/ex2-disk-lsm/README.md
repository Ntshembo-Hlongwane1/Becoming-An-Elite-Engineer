# Exercise 2 — An On-Disk LSM Engine (Files, Pages, Headers, Crashes)

> **What you build:** `DiskDB`, a crash-safe key-value store in a directory: a write-ahead log, sorted
> table files with a **header**, **page-aligned** data blocks (optional), a footer, a MANIFEST of
> VersionEdits, an atomically-renamed CURRENT, recovery, compaction, orphan cleanup — and `lsmdump`,
> a tool that shows you every byte.
>
> **Spec:** `FORMAT.md` (read it fully before writing code). **Reuses:** all of Exercise 1's formats.
>
> **Relation to your object store:** it is *not* an object store. But every mechanism here —
> append-only logs, immutable files with header+footer, checksummed blocks, a manifest + atomic
> pointer switch, orphan cleanup, fail-stop on fsync errors, crash testing with `SIGKILL` — is
> something your object store's metadata and data paths will need. You will recognise them.

Your five goals and where each is taught:

| Your goal | Where in this exercise |
|---|---|
| (a) the data structure | Ex 1 code reused unchanged; D5 levels/compaction on files |
| (b) how it works under the hood | D1 (syscalls), D2 (WAL), D5 (write/read paths), D6 (`lsmdump`) |
| (c) how it links with external storage | D1 `Env`, D3 reading blocks by `pread`, D5 durability protocol |
| (d) page ordering | FORMAT §4.4 alignment, D3 alignment test, E1–E3 experiments, FORMAT §6 durability order |
| (e) metadata, headers | FORMAT §4.1 header vs §4.3 footer, §5 MANIFEST, D4, D6 |

## Build

```bash
cd ex2-disk-lsm            # Exercise 1 must be green first (it is compiled in as a library)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j
./build/dtests             # all          ./build/dtests log    # tests whose name contains "log"
./build/lsmdump /tmp/somedb/000005.sst
```

## Milestones

### D1 — The OS boundary · `./build/dtests env`
Lessons: Part 1 §6–§9, Part 2 §10. Compare with: LevelDB `util/env_posix.cc`.
- `Fd` RAII, `WritableFile` (64 KiB buffer, write loop with partial writes + `EINTR`), `RandomAccessFile`
  (`pread` loop), `SequentialFile`, `FileLock` (`flock`), and the free functions.
- `env_no_fd_leaks` counts `/proc/self/fd` — this is your `master-file-manager` fd-leak bug class made
  impossible by construction.
- Count `preads` and `fsyncs` in `GlobalIoStats()`; the experiments need them.

### D2 — The log · `./build/dtests log`
Lessons: Part 6 §3. Compare with: `db/log_writer.cc`, `db/log_reader.cc`, `doc/log_format.md`.
- The worked example A/B/C must produce exactly the byte layout of notes Part 6 §3 (the test checks
  types, lengths, the 6-byte trailer and total size).
- **Torn tail vs corruption** (FORMAT §2 table): the test truncates the file at *every* byte of the last
  record and requires silent EOF; then flips a byte mid-file and requires a report + resync at the next
  32 KiB block.

### D3 — Tables on disk · `./build/dtests table`
Lessons: Part 6 §4–§8, §12. Compare with: `table/table_builder.cc`, `table/table.cc`, `table/format.cc`.
- The 64-byte header (FORMAT §4.1) — written first, CRC-protected, contains no offsets.
- Packed mode and `block_align = 4096` mode (FORMAT §4.4). The test prints how many packed blocks
  straddle a page boundary vs. 0 in aligned mode, and the size cost of alignment.
- `Table::Get` = at most **one** `pread` after `Open` (the test counts them).
- Flip every byte of a small table: never return different data with an OK status.

### D4 — Names, CURRENT, MANIFEST · `./build/dtests filenames current version manifest db_state`
Lessons: Part 6 §9, §11; Part 1 §8. Compare with: `db/filename.cc`, `db/version_edit.cc`, `db/version_set.cc`.
- `ParseFileName` is a parser of untrusted input (directory listings): the test throws junk at it,
  including `../etc/passwd` in CURRENT.
- `SetCurrentFile` = temp + fsync + rename + fsync(dir); no temp file may survive.
- A torn MANIFEST tail is ignored; a CURRENT pointing nowhere is an error; a foreign comparator is refused.

### D5 — The engine · `./build/dtests db_`
Lessons: Part 5 (all), Part 6 §10–§11; FORMAT §6–§7.
- Port your Exercise 1 `MemDB` logic; runs become files, every structural change becomes a MANIFEST
  edit **committed before** old files are deleted.
- Tests: reopen from WAL only; LOCK; orphans deleted; torn WAL tail tolerated; a corrupted table returns
  `Corruption` (never wrong data); 20 000-op oracle with random reopens.

### D6 — Crash safety + `lsmdump` · `./build/dtests crash` and `tools/lsmdump.cpp`
- The crash test forks a writer, `SIGKILL`s it at random moments (constantly flushing and compacting,
  so kills land mid-protocol), reopens, and checks every acknowledged write. **Run it 20× in a loop**
  — crash bugs are probabilistic. While validating this exercise, the crash test found a real recovery
  bug in my own reference solution within three runs (FORMAT §7 step 5 now documents it). Expect it to
  find yours.
- `lsmdump`: implement the spec at the top of `tools/lsmdump.cpp`. Then **use it**: dump a WAL, a
  table (packed and aligned), a MANIFEST, and annotate one of each by hand in `RESULTS.md`.

### D7 — Challenge: simulated power loss (no tests provided — design them)
`SIGKILL` keeps the page cache, so it cannot catch a missing `fsync`. LevelDB tests this with a
fault-injection environment (`db/fault_injection_test.cc` — read it). Build one:
1. Refactor `env.hpp` behind a virtual `Env` interface.
2. `FaultInjectionEnv` tracks, per file, the size at the last successful `Sync()`, and which new files /
   renames were made durable by a directory sync.
3. `SimulatePowerLoss()` truncates every file to its last-synced size and undoes un-dir-synced creates
   and renames.
4. Test: random workload with `sync=true` → power loss → reopen → every acknowledged write present.
   Then *delete one `Sync`/`SyncDir` call from your engine* and watch which test catches it. Write up
   each in `DECISIONS.md` — this is the most valuable experiment in the whole exercise.

## Experiments (put results + explanations in `RESULTS.md`)

| # | Question | How |
|---|---|---|
| E1 | What does page alignment cost and buy? | Build tables with 1M random 16 B keys / 100 B values, packed vs `block_align=4096`. Compare file size, blocks crossing pages (from `lsmdump`), and `preads`/`pread_bytes` per random `Get`. |
| E2 | Block size trade-off | `block_size` ∈ {1 KiB, 4 KiB, 16 KiB, 64 KiB}: index size, `pread_bytes` per `Get`, full-scan time. Relate to Bigtable's 64 KB vs LevelDB's 4 KB (notes Part 6 §12). |
| E3 | Cold vs warm page cache | Run E1's lookups twice; between runs, evict the file's pages (research `posix_fadvise(2)` with `POSIX_FADV_DONTNEED` and cite the man page). Explain the difference with notes Part 1 §6. |
| E4 | Price of durability | 100 000 `Put`s with `sync=false` vs `sync=true` vs batches of 100 with `sync=true`. Compare with LevelDB's claim "more than a thousand times as fast" (notes Part 5 §3). |
| E5 | Syscall profile | `strace -c -f ./build/your_bench` for a write-heavy and a read-heavy run. Which syscalls dominate? Does your 64 KiB write buffer show up? |
| E6 | Write amplification on disk | Same as Ex 1 M10 but with `GlobalIoStats().write_bytes`; compare logical WA (your Stats) with physical bytes written (include the WAL!). |

## Definition of done

- All `dtests` green under ASan/UBSan; `crash` green 20 runs in a row.
- `lsmdump` works on all four file types; `RESULTS.md` has E1–E6 and annotated dumps.
- `DECISIONS.md`: one sourced line per milestone + differences from LevelDB + D7 findings.
- You can draw, from memory, FORMAT §6's flush protocol and explain what breaks if any two steps swap.
