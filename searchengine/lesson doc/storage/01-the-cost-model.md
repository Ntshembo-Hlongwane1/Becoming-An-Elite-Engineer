# 01 — The Cost Model

> **Build target:** `storage/tests/cost_model_lab.cpp` — a benchmark that measures, on *your*
> machine: the cost of one syscall, the cost of a cold read vs a warm read, and the crossover
> point where reading more bytes stops costing more time. You will finish this doc with
> numbers you measured, not numbers you read.
>
> **Why this is doc 01 and not an appendix.** Every design decision in the next eleven docs is
> a response to a number. If you don't know the numbers on your hardware you are cargo-culting
> a design that was tuned for a spinning disk in 2005. The page size, the buffer pool size,
> the fanout, whether `fsync` is affordable — all of it falls out of this doc.

---

## 1. The only thing that makes storage hard

Here is the entire problem, in one comparison:

```
  L1 cache hit                      ~1 ns        (≈  3 CPU cycles)
  L2 cache hit                      ~4 ns
  L3 cache hit                     ~20 ns
  Main memory (RAM) random read    ~80 ns        (≈ 250 cycles)
  ---------------------------------------------- the cliff -----------
  NVMe SSD random 4 KB read     ~20,000 ns       (≈ 60,000 cycles)
  SATA SSD random 4 KB read     ~80,000 ns
  Spinning disk seek         ~8,000,000 ns       (≈ 24,000,000 cycles)
```

A RAM access is expensive — 250 cycles is 250 instructions you didn't execute. An SSD access
is **250× worse than that**. A disk seek is 24 million cycles: in the time one seek completes,
a modern core could have executed the entire in-memory B+Tree lookup roughly ten thousand
times.

**This is why the B+Tree is shaped the way it is.** Not because n-ary trees are elegant —
because when a single access costs 60,000 cycles, you will do *anything* to have fewer of
them. Fanout 200 instead of 2 turns 20 accesses into 3. That is the whole idea. Everything
else in this series is bookkeeping in service of it.

### The part people miss: the accesses are *serialised*

You cannot overlap them. To know which page to read at level 2, you must have already read
level 1 — the address depends on the data. Modern CPUs and NVMe drives are enormously
parallel and none of that parallelism helps you here. This is a **dependent load chain**, and
its length is the tree height.

```
   read page 1  ──> parse ──> "child is page 87"
                                    |
                                    v
                              read page 87 ──> parse ──> "child is page 4021"
                                                              |
                                                              v
                                                        read page 4021 ──> found
```

Three reads, strictly one after another. 60,000 cycles each, 180,000 total, and no amount of
hardware cleverness compresses it. **Height is latency.** Memorise that; it justifies every
byte you will fight for inside a page in doc 05.

---

## 2. What a syscall actually costs

`ReadPage` will call into the OS. That call is not free even when it touches no hardware,
because it crosses the user/kernel boundary: a mode switch, a page-table check, and — since
Spectre/Meltdown mitigations — a possible TLB flush.

```
  plain function call                     ~1 ns
  syscall (getpid, no I/O)             ~100 ns    on Linux; often 300-1000 ns on Windows
  syscall + page cache hit (no disk)  ~1,000 ns
  syscall + actual NVMe read         ~20,000 ns
```

Three lessons fall out of this, and all three shape the buffer pool:

1. **A cache hit that still makes a syscall is a mostly-wasted cache hit.** This is precisely
   why we build our *own* buffer pool in docs 06–07 rather than leaning on the OS page cache.
   Our hit path is a hash lookup and a pointer return: ~20 ns, no kernel involvement, ~1000×
   faster than asking the OS for the same bytes.
2. **Batch size matters more than you think.** One 4 KB read and one 64 KB read cost nearly
   the same, because the fixed overhead dwarfs the transfer. Doc 12 measures exactly where
   this stops being true on your drive.
3. **`fsync` is in a different universe.** It is not a syscall cost, it is a *durability*
   cost — it must wait for the drive to acknowledge that bytes are in non-volatile storage.
   Budget 0.5–10 **milliseconds**. Doc 11 §4 is about how to not call it on every insert.

---

## 3. The OS page cache — and why your first benchmark will lie

When you read a file, the OS does not hand you bytes from the disk. It hands you bytes from
*its own* cache of that file, filling the cache from disk only on a miss. Consequences:

- The **first** read of a page is slow (disk).
- The **second** read of the same page is fast (RAM), even though your code looks identical.
- After you write a file, it is *already cached*, so reading it back measures RAM speed.

> **This is the exact trap your benchmarking discipline exists to catch.** A benchmark that
> writes a file and immediately reads it back is not measuring disk. It is measuring `memcpy`
> plus syscall overhead, and it will report numbers 20× too good. If you report that number
> as "disk read latency" you have silently reported a degraded measurement — the thing you
> already decided you would never do.

There are three honest ways to get a cold read, in decreasing order of convenience:

| Method | How | Caveat |
|---|---|---|
| **Bigger than RAM** | Make the test file ≥ 2× physical RAM, touch it randomly | Slow to set up, but always honest |
| **Bypass the cache** | Windows: `CreateFile` with `FILE_FLAG_NO_BUFFERING`. Linux: `O_DIRECT` | Requires sector-aligned buffers and offsets |
| **Drop the cache** | Linux: `echo 3 > /proc/sys/vm/drop_caches`. Windows: no supported equivalent | Needs root; not available to you on Windows |

The lab below uses the first method, with an explicit warm-vs-cold comparison so you *see*
the lie rather than being fooled by it.

---

## 4. Why 4096, specifically

Four independent systems picked the same number, which is why it is not really a choice:

1. **The CPU's virtual memory page is 4 KB.** Every mapping, every TLB entry, every
   protection boundary is 4 KB-granular on x86-64.
2. **The OS page cache manages memory in those same 4 KB units.** A read of 4097 bytes
   touches two cache pages.
3. **NVMe and modern SSDs expose 4 KB logical blocks.** Writing 512 bytes to a 4 KB-block
   device is a *read-modify-write*: the drive must read 4 KB, patch it, and write 4 KB back.
   You paid for the whole page whether you used it or not.
4. **Filesystem block size is 4 KB** by default on NTFS and ext4.

So: **any I/O smaller than 4 KB costs the same as 4 KB, and any I/O not aligned to 4 KB costs
double.** That is the entire justification. It is not a tunable in this series.

The corollary drives doc 05: since a page costs the same whether it holds 1 key or 250, you
should cram as many keys in as physically fit. Every byte of header you add is a key you
didn't store, and keys-per-page is the base of the logarithm that sets your tree height.

```
  N = 100,000,000 keys

  fanout   height   disk reads per lookup   time @ 20 us/read
      2       27            27                     540 us
     64        5             5                     100 us
    255        4             4                      80 us
    510        3             3                      60 us
```

Going from fanout 255 to 510 (by halving key size, say) removes an entire level and 25% of
your lookup latency. *That* is why doc 05 counts bytes so carefully.

---

## 5. Sequential vs random

The last number you need before writing code:

```
  NVMe sequential read     ~3,000 MB/s     (~1.3 us per 4 KB page, amortised)
  NVMe random 4 KB read      ~400 MB/s     (~20 us per page)
```

Roughly **15× difference**, and on a spinning disk it is 100×. This is the single fact that
justifies the B+Tree's linked leaf chain: a range scan that follows `next` pointers through
pages that were allocated consecutively is *sequential* I/O. The same scan done by re-descending
from the root for each key is *random* I/O. Same results, 15× the time.

It also explains something you will implement in doc 04 without thinking about it: the free
list hands out recently-freed pages first, which over time scrambles your allocation order
and turns sequential scans random. Real databases fight this constantly; it is called
**fragmentation**, and doc 11 §6 shows you how to measure whether yours has it.

---

## 6. The lab

Write this. Run it. Write the numbers down — docs 05, 07, and 12 will ask you for them.

```cpp
// internal/kernal/core/storage/tests/cost_model_lab.cpp
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <vector>

using Clock = std::chrono::steady_clock;

// Returns nanoseconds per iteration.
template <typename F>
double timeIt(F&& fn, int iterations) {
    // Warm up: page in code, populate branch predictors, let the CPU clock ramp.
    // Skipping this reports the cost of a cold instruction cache, not of the work.
    for (int i = 0; i < iterations / 10 + 1; ++i) fn();

    auto start = Clock::now();
    for (int i = 0; i < iterations; ++i) fn();
    auto end = Clock::now();

    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    return static_cast<double>(ns) / iterations;
}

int main() {
    constexpr std::size_t PAGE = 4096;

    // ---------------------------------------------------------------- 1. syscall floor
    // Every ReadPage you write in doc 03 pays this before it moves a single byte.
    {
        std::ofstream sink("sink.tmp", std::ios::binary);
        char byte = 0;
        double ns = timeIt([&]{
            sink.write(&byte, 1);
            sink.flush();              // forces the write() syscall, not just a buffer append
        }, 20000);
        std::cout << "syscall floor (write+flush 1 byte): " << ns << " ns\n";
    }
    std::remove("sink.tmp");

    // ---------------------------------------------------------------- 2. build a big file
    // MUST exceed physical RAM to defeat the OS page cache. Raise this if your box has
    // lots of RAM -- if the whole file fits in cache, section 4 below measures RAM, not disk,
    // and every conclusion you draw from it will be wrong.
    constexpr std::size_t FILE_PAGES = 512 * 1024;          // 512K pages * 4 KB = 2 GB
    const char* path = "cost_lab.tmp";

    {
        std::cout << "building " << (FILE_PAGES * PAGE) / (1024 * 1024) << " MB test file...\n";
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        std::vector<char> buf(PAGE, 'x');

        auto start = Clock::now();
        for (std::size_t i = 0; i < FILE_PAGES; ++i) {
            // Stamp the page id into the first 8 bytes so section 4 can verify it read
            // the page it asked for. An unverified I/O benchmark is a benchmark of nothing:
            // a bug that returns the wrong page looks exactly like a very fast correct one.
            std::memcpy(buf.data(), &i, sizeof(i));
            out.write(buf.data(), PAGE);
        }
        out.flush();
        auto end = Clock::now();

        double sec = std::chrono::duration<double>(end - start).count();
        std::cout << "sequential write: "
                  << (FILE_PAGES * PAGE) / (1024.0 * 1024.0) / sec << " MB/s\n";
    }

    // ---------------------------------------------------------------- 3. warm read (THE LIE)
    // We just wrote this file, so the tail of it is sitting in the OS page cache.
    // This number is RAM speed wearing a disk costume. Print it precisely so you can
    // compare it to the honest number below and never confuse the two again.
    {
        std::ifstream in(path, std::ios::binary);
        std::vector<char> buf(PAGE);
        std::size_t hotPage = FILE_PAGES - 1;

        double ns = timeIt([&]{
            in.seekg(static_cast<std::streamoff>(hotPage * PAGE));
            in.read(buf.data(), PAGE);
        }, 20000);
        std::cout << "WARM read (cached, misleading): " << ns << " ns\n";
    }

    // ---------------------------------------------------------------- 4. cold random read
    // Random offsets across a file larger than RAM: most reads miss the page cache and
    // reach the device. This is the number the whole series is designed around.
    {
        std::ifstream in(path, std::ios::binary);
        std::vector<char> buf(PAGE);
        std::mt19937_64 rng(12345);
        std::uint64_t checksum = 0;

        auto start = Clock::now();
        constexpr int READS = 2000;
        for (int i = 0; i < READS; ++i) {
            std::size_t page = rng() % FILE_PAGES;
            in.seekg(static_cast<std::streamoff>(page * PAGE));
            in.read(buf.data(), PAGE);

            std::uint64_t stamped;
            std::memcpy(&stamped, buf.data(), sizeof(stamped));
            if (stamped != page) { std::cout << "CORRUPT read!\n"; return 1; }
            checksum += stamped;                 // also stops the optimiser deleting the read
        }
        auto end = Clock::now();

        double ns = std::chrono::duration<double, std::nano>(end - start).count() / READS;
        std::cout << "COLD random 4 KB read: " << ns << " ns  ("
                  << ns / 1000.0 << " us)   [checksum " << checksum << "]\n";
    }

    // ---------------------------------------------------------------- 5. size crossover
    // Where does reading more bytes start actually costing more time? Below that size,
    // you are paying pure overhead -- which is the argument for a 4 KB (or larger) page.
    {
        std::ifstream in(path, std::ios::binary);
        std::mt19937_64 rng(999);

        for (std::size_t sz : {64u, 512u, 4096u, 16384u, 65536u, 262144u}) {
            std::vector<char> buf(sz);
            auto start = Clock::now();
            constexpr int N = 500;
            for (int i = 0; i < N; ++i) {
                std::size_t maxOff = FILE_PAGES * PAGE - sz;
                std::size_t off = (rng() % (maxOff / sz)) * sz;
                in.seekg(static_cast<std::streamoff>(off));
                in.read(buf.data(), static_cast<std::streamsize>(sz));
            }
            auto end = Clock::now();
            double ns = std::chrono::duration<double, std::nano>(end - start).count() / N;
            std::cout << "  read " << sz << " B: " << ns << " ns  ("
                      << (sz / 1024.0) / (ns / 1e9) / 1024.0 << " MB/s)\n";
        }
    }

    std::remove(path);
    return 0;
}
```

Build it:

```bash
g++ -std=c++20 -O2 -Wall -Wextra cost_model_lab.cpp -o cost_model_lab
./cost_model_lab
```

`-O2` matters. An unoptimised benchmark measures the compiler's debug output, not your
hardware. It matters in the *other* direction too — that `checksum` accumulator exists so the
optimiser cannot prove the reads are dead and delete them. Benchmarks that measure nothing
are the most common kind.

---

## 7. Reading your results

Fill this in from your own run:

| Measurement | Your number | What it means |
|---|---|---|
| Syscall floor | _______ ns | The tax on every `DiskManager` call. Doc 06's cache exists to avoid it. |
| Warm read | _______ ns | The lie. Never quote this as disk latency. |
| Cold random read | _______ ns | Your real cost per tree level. Multiply by height for lookup latency. |
| Warm ÷ Cold ratio | _______ × | How badly a careless benchmark would have misled you. |
| Crossover size | _______ B | Below this, bigger reads are free. Should be ≥ 4096 — that's why we chose it. |

Two sanity checks:

- **If cold ≈ warm**, your file was not bigger than RAM. Raise `FILE_PAGES` and rerun; you did
  not measure the disk.
- **If cold random is under ~5 µs**, you are on a fast NVMe (good) — but re-check the file
  size anyway, because that is also what a fully-cached file looks like.

---

## 8. What this buys you for the rest of the series

Every later decision now has a number behind it:

- **Doc 05 counts bytes** because fanout sets height and height multiplies your cold-read
  number.
- **Docs 06–07 build a buffer pool** because your syscall floor showed that even a *cached*
  OS read costs ~1000× a hash lookup.
- **Doc 08 builds RAII guards** because a leaked pin permanently removes a frame from a pool
  whose whole purpose is to keep your cold-read count near zero.
- **Doc 11 batches `fsync`** because durability costs milliseconds and your inserts cost
  microseconds.
- **Doc 12 revisits all of it** with `pread`/`pwrite` and asks whether the syscall floor moved.

---

## Checkpoint

Before doc 02, you should have:

- [ ] `cost_model_lab.cpp` compiling with `-O2 -Wall -Wextra`, no warnings
- [ ] The table in §7 filled in with **your** numbers
- [ ] A one-line answer to: *if your tree has height 4 and every level misses cache, what is
      your lookup latency?* (Multiply. That number is your budget for the rest of the series.)
- [ ] The warm-vs-cold ratio written down somewhere you'll see it again — it is the size of
      the mistake you are now immune to

Next: [02 — The Page](02-the-page.md), where that 4096 becomes a type.
