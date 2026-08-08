# 12 — The Latency Lab

> **Build target:** no new component. Instead: six candidate optimisations, each measured
> against the engine you now have, each kept or discarded **on the number**. At the end you
> will have a faster engine and — more valuable — a defensible list of things that did *not*
> help on your hardware.
>
> **The discipline this doc enforces:** benchmark before, benchmark after, keep the change only
> if the number moved outside the noise. Several of the six below will not help you. That is
> the finding, not a failure. An engineer who knows which optimisations are myths on their
> machine is worth more than one who applies all of them.

---

## 1. Measure before you touch anything

You cannot optimise what you have not profiled, and you cannot profile what you have not
isolated. Build this first.

```cpp
// storage/tests/latency_bench.cpp
#include "../../datastructures/bplustree/DiskBPlusTree.hpp"
#include <chrono>
#include <iostream>
#include <iomanip>
#include <random>
#include <vector>
#include <algorithm>

using Clock = std::chrono::steady_clock;

struct Stats { double p50, p99, mean; };

static Stats Summarise(std::vector<double>& samples) {
    std::sort(samples.begin(), samples.end());
    double sum = 0; for (double s : samples) sum += s;
    return { samples[samples.size() / 2],
             samples[static_cast<std::size_t>(samples.size() * 0.99)],
             sum / samples.size() };
}

int main() {
    constexpr int N = 1000000;
    std::remove("bench.db");

    DiskManager dm("bench.db");
    BufferPool  bp(dm, 4096);              // 16 MB pool
    DiskBPlusTree tree(dm, bp);

    // ---- build ----
    auto t0 = Clock::now();
    for (int k = 0; k < N; ++k)
        tree.Insert(k, PostingRef{static_cast<std::uint64_t>(k), 1});
    tree.Checkpoint();
    double buildSec = std::chrono::duration<double>(Clock::now() - t0).count();

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "build: " << buildSec << " s  ("
              << N / buildSec / 1000 << "K inserts/s), height=" << tree.Height()
              << ", pages=" << dm.NumPages() << "\n";

    // ---- point lookups: measure the DISTRIBUTION, not just the mean ----
    std::mt19937 rng(1);
    std::vector<double> samples;
    samples.reserve(100000);

    for (int i = 0; i < 100000; ++i) {
        disk_key_t k = rng() % N;
        auto s = Clock::now();
        PostingRef v;
        bool found = tree.Search(k, v);
        auto e = Clock::now();
        if (!found) { std::cout << "MISSING KEY -- benchmark is invalid\n"; return 1; }
        samples.push_back(std::chrono::duration<double, std::nano>(e - s).count());
    }

    Stats s = Summarise(samples);
    std::cout << "lookup  p50=" << s.p50 << " ns  p99=" << s.p99
              << " ns  mean=" << s.mean << " ns\n";
    std::cout << "buffer pool hit rate: " << bp.HitRate() * 100 << "%\n";
    std::cout << "disk reads: " << dm.ReadCount() << ", writes: " << dm.WriteCount() << "\n";

    // ---- range scan ----
    auto r0 = Clock::now();
    auto results = tree.RangeSearch(0, N);
    double scanSec = std::chrono::duration<double>(Clock::now() - r0).count();
    std::cout << "full scan: " << scanSec << " s ("
              << results.size() / scanSec / 1e6 << "M keys/s), locality="
              << tree.ChainLocality() << "\n";
}
```

### Why p99 and not just the mean

The mean hides everything that matters. A workload with a 98% buffer pool hit rate has a mean
dominated by 20 ns hits, while the 2% that reach disk cost 20,000 ns each. **The mean tells you
about the cache; p99 tells you about the disk.** Users experience p99.

If your p50 is ~200 ns and your p99 is ~20,000 ns, that gap *is* the story: p50 is a fully
cached descent, p99 is one cold read. Every optimisation below either shrinks the number of
cold reads (p99) or the cost of a cached descent (p50) — and knowing which one you are aiming
at prevents most wasted effort.

### Rules for every measurement in this doc

1. **`-O2` minimum.** An unoptimised benchmark measures the compiler.
2. **Consume the result.** If nothing reads `v`, the optimiser may delete the search entirely.
3. **Same file, same seed, same machine state.** Close your browser.
4. **Run each variant three times.** If the spread between runs exceeds the difference between
   variants, you have measured nothing. Say so and move on.
5. **Change one thing at a time.**

---

## 2. Candidate 1 — `pread`/`pwrite` instead of seek+read

**The claim:** one syscall instead of two, and no shared file position.

```cpp
// DiskManager, native descriptor path
#if defined(_WIN32)
  #include <io.h>
  static std::size_t ReadAt(int fd, void* buf, std::size_t n, std::int64_t off) {
      // Windows has no pread; emulate with an OVERLAPPED ReadFile on the HANDLE.
      HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
      OVERLAPPED ov{};
      ov.Offset     = static_cast<DWORD>(off & 0xFFFFFFFF);
      ov.OffsetHigh = static_cast<DWORD>(off >> 32);
      DWORD got = 0;
      if (!ReadFile(h, buf, static_cast<DWORD>(n), &got, &ov)) {
          if (GetLastError() != ERROR_HANDLE_EOF) throw std::runtime_error("ReadFile failed");
      }
      return got;
  }
#else
  static std::size_t ReadAt(int fd, void* buf, std::size_t n, std::int64_t off) {
      ssize_t got = ::pread(fd, buf, n, off);
      if (got < 0) throw std::runtime_error("pread failed");
      return static_cast<std::size_t>(got);
  }
#endif
```

**What to expect.** The syscall saving is real but small — you disabled stdio buffering in doc
03, so `fseek` was already cheap. Expect single-digit percent on p99, nothing on p50.

**The real reason to do it** is not speed: `pread` takes the offset as an argument and never
touches a shared file position. That makes it **safe to call from multiple threads on one
descriptor**, which the `fseek`+`fread` pair is not. It also removes the `const`-method wart
doc 03 flagged in `NumPages`.

**Verdict guidance:** keep it for the concurrency property even if the number barely moves. Note
that honestly in your commit message rather than claiming a speedup you did not measure.

---

## 3. Candidate 2 — branchless binary search

**The claim:** the branch in binary search is unpredictable by construction, so every probe
costs a ~15-cycle misprediction.

Your current inner loop:

```cpp
while (lo < hi) {
    std::size_t mid = lo + (hi - lo) / 2;
    if (key < KeyAt(mid)) hi = mid; else lo = mid + 1;   // 50/50 -- unpredictable
}
```

Branchless via conditional move:

```cpp
std::size_t NodePage::UpperBoundBranchless(disk_key_t key) const {
    std::size_t base = 0;
    std::size_t n    = KeyCount();
    while (n > 1) {
        const std::size_t half = n / 2;
        // No branch: the compiler emits CMOV. Both sides are computed; neither is predicted.
        base = (KeyAt(base + half - 1) < key || !(key < KeyAt(base + half - 1)))
                   ? base + half : base;
        n -= half;
    }
    return base + (n > 0 && !(key < KeyAt(base)) ? 1 : 0);
}
```

**What to expect.** In a microbenchmark over a hot array: 1.5–2× on the search alone. In your
*engine*: likely invisible, because a search is ~8 probes at a few nanoseconds each against a
descent that costs hundreds of nanoseconds or tens of microseconds.

**This is the most important lesson in the doc.** A genuine 2× win on 2% of your time is a 1%
win overall. Amdahl's law is not a slogan; it is the reason most micro-optimisation is wasted
effort. Measure the *whole operation*, not the function you are excited about.

Verify the compiler actually emitted `cmov` (`-S` and grep, or godbolt). Often it does not, and
you have written unreadable code for nothing.

---

## 4. Candidate 3 — prefetching the child page

**The claim:** while binary-searching the current node, issue a prefetch for the child you will
need next, overlapping the memory latency with useful work.

```cpp
const std::size_t idx  = node.UpperBoundIndex(key);
const page_id_t   next = node.ChildAt(idx);

// Hint: we are about to touch this frame. Get the line moving now.
if (Page* p = m_Pool.PeekResident(next)) {          // no pin, no I/O, just a lookup
    __builtin_prefetch(p->data, 0, 3);
}
```

**What to expect.** Modest and only on cached descents. The dependent-load chain from doc 01 §1
is the fundamental limit: you cannot prefetch the grandchild, because its address is inside the
child you have not read yet. Prefetching only helps for the *one* step you can see ahead.

**Where it genuinely wins is I/O, not cache.** If you can predict several pages ahead — as a
`RangeSearch` can, since the leaf chain is known — you can issue asynchronous reads for the next
few leaves and overlap them. That is worth far more than a cache-line hint, and it is the right
place to spend this effort:

```cpp
// In RangeSearch: warm the next leaf while processing the current one.
const page_id_t lookahead = leaf.NextLeaf();
if (lookahead != INVALID_PAGE_ID) m_Pool.PrefetchAsync(lookahead);
```

---

## 5. Candidate 4 — `memmove` for the insert shift

**The claim:** the element-by-element shift loop in `InsertIntoLeaf` is slow compared to a
vectorised `memmove`.

```cpp
// Legal precisely because doc 05 section 5 laid keys out contiguously.
void NodePage::ShiftKeysRight(std::size_t from, std::size_t count) {
    std::byte* base = m_Page.data + NODE_HEADER_SIZE;
    std::memmove(base + (from + 1) * KEY_SIZE,
                 base + from * KEY_SIZE,
                 (count - from) * KEY_SIZE);
}
```

**What to expect.** A real win on the insert path — `memmove` is vectorised and the loop is
not, because the compiler cannot prove the accessors are simple loads and stores. Expect
10–30% on insert throughput with full pages.

**Note what makes this possible.** SoA layout. Under AoS you would need two interleaved shifts
with a stride, and `memmove` would not apply. The layout decision from doc 05 §5 — made for
cache-line reasons on the *search* path — turns out to also enable this on the *insert* path.
Good layout decisions pay repeatedly.

---

## 6. Candidate 5 — kill the path-stack allocation

Doc 09 §4 used `std::vector<page_id_t>` for the search path. Every `Insert` allocates and frees
it. Tree height is at most 5 or 6.

```cpp
struct SearchPath {
    static constexpr std::size_t MAX_DEPTH = 16;
    page_id_t   nodes[MAX_DEPTH];
    std::size_t slots[MAX_DEPTH];
    std::size_t depth = 0;

    void Push(page_id_t id, std::size_t slot) {
        if (depth >= MAX_DEPTH) throw std::runtime_error("tree deeper than MAX_DEPTH");
        nodes[depth] = id; slots[depth] = slot; ++depth;
    }
    void Pop()        { --depth; }
    bool Empty() const { return depth == 0; }
};
```

**What to expect.** A measurable insert-throughput win, because you removed a `malloc`/`free`
pair from the hot path — 50–100 ns each, on an operation that may otherwise be a few hundred
nanoseconds when cached.

**Why this one is reliably worth it** while candidate 3 was not: allocation is not just slow,
it is *unpredictable* — it can take a lock, it can touch cold memory, it can trigger a page
fault. Removing an allocation from a hot path improves the tail (p99), which is the number that
actually matters. **Prefer optimisations that remove variance over ones that shave a few
cycles.**

---

## 7. Candidate 6 — pool size

Not code. Just the most effective knob you have, and the one people forget to turn.

```
  pool    memory   hit rate   p50      p99
    64     256 KB    ?         ?        ?
   512       2 MB    ?         ?        ?
  4096      16 MB    ?         ?        ?
 32768     128 MB    ?         ?        ?
```

Fill it in from your own runs. Expect a knee: hit rate climbs steeply until the pool holds the
tree's upper levels, then flattens. Past that knee you are buying leaf caching, which only pays
if your workload has locality.

**The knee is the answer.** For a 4-level tree with fanout 340, the root plus level 2 is about
340 pages — 1.4 MB. That tiny pool already eliminates 2 of every 4 disk reads. Everything beyond
it has sharply diminishing returns, and you now know that as a measured fact about your engine
rather than a guess.

---

## 8. Things not to do

| Tempting | Why not |
|---|---|
| `-O3` over `-O2` | Usually noise; sometimes slower from code bloat. Measure; almost never worth it. |
| `-march=native` | Real gains sometimes, but the binary stops running on other machines. Fine for a benchmark, dangerous to ship. |
| Bigger pages (8K, 16K) | Reduces height *logarithmically* while raising I/O cost *linearly*. Measure before believing; 4096 usually wins. |
| Compressing pages | Trades CPU for I/O. Only wins when I/O-bound, which a well-tuned pool means you often are not. |
| Threading the buffer pool | Correctness cost is enormous (doc 11 §7). Exhaust single-thread wins first. |
| `inline` everywhere | The compiler decides. `inline` is a linkage keyword, not a performance one. |

---

## 9. The report

Write this up. It is the deliverable of the whole series.

```
  ENGINE: DiskBPlusTree, 1M keys, 4096-frame pool, <your CPU / SSD>

  BASELINE
    build            ____ K inserts/s
    lookup p50/p99   ____ / ____ ns
    scan             ____ M keys/s
    hit rate         ____ %

  CANDIDATE                     p50      p99     insert/s    KEEP?
  1 pread/pwrite               ____     ____      ____       yes (concurrency, not speed)
  2 branchless search          ____     ____      ____       ____
  3 prefetch child             ____     ____      ____       ____
  4 memmove shift              ____     ____      ____       ____
  5 fixed-size path stack      ____     ____      ____       ____
  6 pool size 512 -> 4096      ____     ____      ____       ____

  CONCLUSION: <which two actually mattered, and why>
```

If your honest conclusion is "candidates 4, 5, and 6 mattered; 2 and 3 were noise", that is a
**correct and valuable** result. It says your engine is bound by allocation and I/O rather than
by ALU work, which tells you where to look next.

---

## Where this leaves you

You have built, from nothing:

- a page abstraction with enforced size and alignment
- a disk manager doing real offset arithmetic, real syscalls, and real `fsync`
- an on-disk allocator with an intrusive free list
- a byte-exact node format whose fanout is derived from physics
- a buffer pool with pin counts, dirty tracking, and O(1) LRU eviction
- RAII guards making pin leaks unwritable
- a B+Tree doing search, range scan, insert, split, delete, borrow, and merge — entirely on
  pages, surviving process death
- a measured, defensible account of what makes it fast

**What you should be able to do now** that you could not before doc 01: read the SQLite file
format spec and recognise every structure in it; read BoltDB's `node.go` and see your own
`SplitLeaf`; look at a `perf` profile and know whether you are bound by I/O, cache, or
allocation.

### Where to go next

- **Concurrency** — latch crabbing (doc 11 §7). The hardest remaining piece, and CMU 15-445
  Project 2 does exactly this if you want a graded version.
- **WAL** — doc 11 §4's sketch, built out. Turns "durable at checkpoints" into "durable per
  operation" without paying `fsync` per page.
- **Variable-length keys** — doc 05 §8's slotted page. Needed for a real term dictionary.
- **LSM trees** — the other answer to the same problem. Now that you know what B+Trees cost on
  writes, you are equipped to understand why search engines usually choose differently.
- **The posting list file** — the other half of your search engine. The tree gives you a
  `PostingRef`; something has to encode, compress, and intersect what it points at.

Back to [00 — Index](00-index.md).
