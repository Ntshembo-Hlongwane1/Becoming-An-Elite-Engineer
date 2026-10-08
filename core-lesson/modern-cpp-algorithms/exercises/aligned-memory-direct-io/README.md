# Aligned Memory & Direct I/O — Exercises

Six exercises, **beginner → advanced**, for the lesson in
`../../notes/aligned-memory-direct-io/`. Each one is a building block for the next: Ex 1's
arithmetic is used by Ex 2 and 3, Ex 3's buffer is used by Ex 5, and Ex 5's file is used by Ex 6,
which finally fixes your `notes/drills/drill1.cpp`.

| Ex | Level | Files you implement | Test filter | Do it after |
|---|---|---|---|---|
| 1 | beginner | `src/align.cpp` | `./build/tests ex1` | Part 1 |
| 2 | beginner+ | `src/aligned_malloc.cpp` | `./build/tests ex2` | Part 3 |
| 3 | intermediate | `src/aligned_buffer.cpp` | `./build/tests ex3` | Part 4 §1–3 |
| 4 | intermediate+ | `include/am/aligned_allocator.hpp` (the TODOs) | `./build/tests ex4` | Part 4 §4–5 |
| 5 | advanced | `src/direct_file.cpp` | `./build/tests ex5` | Part 5 |
| 6 | advanced | `bench/drill_bench.cpp` + `RESULTS.md` + port the fix to `notes/drills/` | (measurement, no tests) | Part 6 |

## Rules of engagement (same as the LSM exercises)

1. **Tests are the spec.** Headers declare the API and its contract in comments. Bodies call
   `Todo(...)`. Replace every `Todo` until `./build/tests` is green. You may change **private**
   members and add files; keep the **public** API.
2. **In order.** Don't start Ex N+1 until Ex N is green **under ASan + UBSan** (on by default in
   Debug). ASan is half the test suite here: undersized blocks, wrong-pointer frees and mismatched
   deallocators are *its* findings (notes Part 3 §8).
3. **Every decision gets one line in `DECISIONS.md`**, with the source (lesson part / man page /
   cppreference / RocksDB file) that justified it.
4. **Build before you read production code.** After each exercise is green, read the matching
   production code and write down the differences:
   - Ex 1–2 → LevelDB `util/arena.cc` (`AllocateAligned`), RocksDB `util/aligned_buffer.h`
   - Ex 3 → RocksDB `AlignedBuffer` (what does it do that yours doesn't?)
   - Ex 4 → libstdc++ `/usr/include/c++/15/bits/new_allocator.h` (`std::allocator`'s `allocate`)
   - Ex 5 → RocksDB `env/io_posix.cc` (`PosixRandomAccessFile::Read`, `PosixWritableFile::Close`)
     and `file/writable_file_writer.cc` (`WriteDirect`)
5. **Run the tests from a real disk.** Ex 5 creates files in the current directory. On tmpfs
   (`/tmp`), O_DIRECT accepts anything (notes Part 5 §6) and one test SKIPs.

## Build & run

```bash
cd core-lesson/modern-cpp-algorithms/exercises/aligned-memory-direct-io
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/tests            # everything (expect TODOs at first)
./build/tests ex1        # only Exercise 1
```

Output states: `PASS`, `FAIL` (with file:line and values), `TODO` (still a stub), `SKIP` (environment).
**ASan reports appear after the summary line** (leaks are only checked at process exit), so read
the whole output: a run that says "all passed" and then prints an ASan leak report has **failed**.

## Exercise notes

### Ex 1 — Alignment arithmetic (`include/am/align.hpp`)
No `%`, no `/`, no `<bit>`. Masks only. The overflow test is CVE-2013-4332's bug class (notes
Part 7 §1). Your `AlignUp` must refuse before the addition wraps. A randomized test compares you
against RocksDB's `Roundup` formula computed in 128-bit.
*DECISIONS.md:* compare your `IsPowerOfTwo` / `NaturalAlignment` with `std::has_single_bit` /
`std::countr_zero`; look at the generated assembly for both (`g++ -O2 -S`).

### Ex 2 — `AlignedMalloc` / `AlignedFree` on plain `malloc` (`include/am/aligned_malloc.hpp`)
The header trick from notes Part 3 §7c. ASan will tell you immediately if `AlignedFree` frees the
wrong pointer ("attempting free on address which was not malloc()-ed").
*DECISIONS.md:* worst-case overhead for `align = 4096`. Compare with the measured 8192-byte spacing
of glibc's `aligned_alloc` (notes Part 3 §10).

### Ex 3 — `AlignedBuffer` (`include/am/aligned_buffer.hpp`)
Notes Part 4 §3 justifies every member. Zero-filled capacity is a *security* requirement (Part 7
§4). The self-move test catches "free first, then steal".
*DECISIONS.md:* raw pointer + hand-written moves, or `unique_ptr<char, FreeDeleter>` + `= default`?
Which did you choose, and why?

### Ex 4 — `AlignedAllocator<T, Align>` (`include/am/aligned_allocator.hpp`)
Make `std::vector<char, AlignedAllocator<char, 4096>>` aligned through every reallocation, and make
`std::list`/`std::map` work (that needs `rebind` + the converting constructor, notes Part 4 §5).
*DECISIONS.md:* delete `rebind`, paste the compiler error, and explain it with
`[CPPREF-allocator_traits]`.

### Ex 5 — `DirectFile` + `DirectAppender` (`include/am/direct_file.hpp`)
The real thing: `statx(STATX_DIOALIGN)`, precise `MisalignedIo` errors instead of a bare `EINVAL`,
`pwrite`/`pread` loops, RAII fd with no double close (notes Part 6 B1), and an appender that
writes only whole aligned blocks yet produces byte-exact files (the RocksDB tail-rewrite +
`ftruncate` design, notes Part 5 §7).
*DECISIONS.md:* (a) what you do on a partial direct write that isn't block-aligned (Part 6 B3);
(b) why the appender's destructor swallows exceptions and what that costs you; (c) what
`device_bytes_written / logical_size` was in `ex5_appender_flush_then_continue_rewrites_tail` and
why.

### Ex 6 — Measure it (`bench/drill_bench.cpp`, `RESULTS.md`)
Rebuild your drill on Ex 3 + Ex 5 as a proper experiment (the instructions are at the top of
`bench/drill_bench.cpp`), fill in `RESULTS.md`, then port the fix back into
`notes/drills/drill1.cpp` + `include/file.{hpp,cpp}` so the original drill runs cleanly and none
of the findings in notes Part 6 remain.

## Definition of done

- `./build/tests` all green under ASan/UBSan, with **no** sanitizer report after the summary.
- `DECISIONS.md` has ≥ 2 sourced lines per exercise and a "differences from RocksDB/LevelDB" list.
- `RESULTS.md` has your measured table and an explanation of every number.
- `notes/drills/drill1.cpp` runs on your ext4 repo directory without errors.
- You can answer, without notes: *Why did `write()` return `EINVAL`, which three things must be
  aligned, to what on this machine, how do you find that out at run time, and which function
  must free the buffer you used?*
