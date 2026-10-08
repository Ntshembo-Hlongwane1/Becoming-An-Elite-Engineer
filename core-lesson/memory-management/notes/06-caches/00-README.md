# Lesson 6 — Caches and the Memory Hierarchy

> Status: complete. Exercise: `../../exercises/06-caches/`.
>
> So far "memory" has been one flat array. Physically it is a **hierarchy**: a few fast registers, a
> little fast cache, a lot of slow DRAM, with ~100× latency differences between levels. How your data
> is laid out and traversed decides which level you hit, and therefore how fast your program runs —
> often by 5–10× with no change in the algorithm's big-O. This is the performance half of memory
> management, and the measurement methodology here is exactly what your capstone's write-up (and your
> red-team performance analyses) will use.

Read in order:
1. `01-the-memory-hierarchy.md` — registers → L1/L2/L3 → DRAM; latencies; cache lines; your measured hierarchy.
2. `02-locality-and-cache-lines.md` — spatial/temporal locality; row- vs column-major (measured 4.8×); AoS vs SoA.
3. `03-blocking-and-prefetch.md` — cache blocking/tiling; prefetch; aligning hot data to lines.
4. `04-false-sharing-and-coherence.md` — cache coherence; false sharing (measured ~5×); `alignas(64)`.
5. `05-security-view.md` — cache timing side channels, conceptually and defensively; three hats.
6. `06-glossary.md`.

Then do the exercise: **cache-aware transforms + a timing harness** — a blocked matrix transpose you
prove correct, plus a benchmark that reproduces the 4.8× and 5× effects on your machine for your
write-up.

## The one-paragraph picture
The CPU can't wait ~100 ns for DRAM on every access, so it keeps recently/nearby-used bytes in small
fast **caches** (L1 ~1 ns, L2, L3), moving data between levels in 64-byte **cache lines**. Code that
touches memory with **spatial locality** (consecutive addresses — a cache line serves several
accesses) and **temporal locality** (reusing data while it's still cached) runs from L1; code that
strides across memory misses to DRAM every time. Because a whole line moves at once, two threads
writing *different* variables that happen to share a line fight over it (**false sharing**), and one
hot variable per line (`alignas(64)`) fixes it. None of this changes your algorithm's complexity —
it changes the constant by an order of magnitude, which is why it is a first-class memory-management
concern.
