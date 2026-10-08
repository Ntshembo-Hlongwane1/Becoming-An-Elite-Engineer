// Exercise 6 (advanced) — Your drill, rebuilt on Exercises 3 + 5, as a real experiment.
// Notes: Part 6 (the diagnosis of your original drill), Part 5 §11 (what to measure and why).
//
// Build in Release for real numbers (sanitizers distort timing):
//   cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release && cmake --build build-rel --target drill_bench
//   ./build-rel/drill_bench <dir-on-a-real-disk> > results.txt
//
// What to implement (no code is given on purpose — every line is a decision you can now justify):
//
//  1. Parse args: target directory (default "."), total bytes (default 64 MiB = your drill's size).
//  2. For mode in {kBuffered, kDirect} x block_size in {4 KiB, 64 KiB, 1 MiB}:
//       a. DirectFile::Open(dir/"bench.bin", O_RDWR|O_CREAT|O_TRUNC, mode)   — Part 6 D3 (O_TRUNC)
//       b. AlignedBuffer::Allocate(block_size, file.alignment().memory), fill it with non-zero bytes
//       c. SEQUENTIAL: WriteAt(buf, i * block_size) for i in [0, n); then Sync(). Time both parts
//          separately (Part 5 §11: "write-loop" vs "fsync").
//       d. RANDOM: the same n blocks, offsets i * block_size for i in a SHUFFLED permutation
//          (std::shuffle with a fixed seed) — NOT i*5 (Part 6 A2). Then Sync(). Time it.
//       e. One output line per (mode, block size, pattern). No printing inside loops (Part 6 D1).
//  3. Print the filesystem's DIO alignment and whether it was reported (Part 5 §6).
//  4. Bonus: a DirectAppender run that appends random-sized records (1..8000 bytes) and Flushes every
//     k records for k in {1, 10, 100}; report device_bytes_written / logical_size (write amplification
//     from tail rewrites — Part 5 §7) and the time.
//
// Then fill in RESULTS.md: the table, and an explanation of every number using Part 5. In
// particular: why is 4 KiB O_DIRECT so slow (compute µs per write), where does buffered I/O pay its
// cost, and does random vs sequential matter on your (virtual) disk? Why or why not?
//
// Finally, port the fix back to notes/drills/drill1.cpp + include/file.{hpp,cpp} so that drill
// compiles, runs on your ext4 repo directory without EINVAL, and has none of the Part 6 findings.

#include <cstdio>

int main() {
    std::puts("TODO(Ex6): implement the benchmark described at the top of bench/drill_bench.cpp");
    return 1;
}
