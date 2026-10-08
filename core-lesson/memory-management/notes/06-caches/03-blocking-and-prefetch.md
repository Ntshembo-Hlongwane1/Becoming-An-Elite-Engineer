# 6.3 — Cache Blocking and Prefetch

Two techniques to keep a computation in cache even when the whole data set doesn't fit.

## 1. Blocking (tiling)

`[ALGO §22.2]` ("Algorithms Optimized for Cache (Blocking, Tiling)"): process the data in
cache-sized **blocks** so that data loaded into cache is fully reused before it's evicted.

The classic case is a matrix transpose or multiply. A naive transpose `B[c][r] = A[r][c]` reads `A`
row-major (good) but writes `B` column-major (bad, §6.2) — one side always thrashes. **Blocking**
transposes small tiles (say 16×16, chosen so a tile of both `A` and `B` fits in L1):

```
for (rb = 0; rb < N; rb += T)
  for (cb = 0; cb < N; cb += T)
    for (r = rb; r < min(rb+T,N); ++r)
      for (c = cb; c < min(cb+T,N); ++c)
        B[c*N + r] = A[r*N + c];     // operate within a T×T tile that stays in cache
```

Same result as the naive transpose, but each tile's lines are loaded once and fully used. `[ALGO
§22.1.6]` note 4 ("blockSize tuned to fit cache line sizes") and §22.2 are this idea. Your exercise
implements a blocked transpose and proves it equals the naive one, then times both. Tile size is a
tuning knob: too small wastes the per-tile overhead, too big spills the tile out of L1.

## 2. Prefetching

The hardware **prefetcher** watches your access pattern and pulls lines ahead of use — which is *why*
sequential access is so fast (it predicts the next line). You can also prefetch explicitly when the
pattern is predictable but not linear `[ALGO §22.1.6]`:

```cpp
__builtin_prefetch(&data[i + 16]);   // ask for a line ~16 elements ahead while working on i
```

Use it sparingly and measure — a wrong prefetch wastes bandwidth and cache. The biggest "prefetch"
win is usually just *making the pattern sequential* so the hardware prefetcher does it for free (§6.2).

## 3. Aligning hot data to lines

`[MEM §9.3]`, `[MEM §17.5]`, `[ALGO §22.1.6]`: align frequently-accessed data to a cache-line boundary
(`alignas(64)` or `std::aligned_alloc`, alignment lesson Part 3) so a hot datum sits in exactly one
line instead of straddling two (which would cost two fetches, alignment lesson Part 1 §11). This is the
read-side twin of the false-sharing fix (§6.4), which is the write-side.

## 4. The method, not just the tricks
The real skill is the **measure → hypothesize → change → measure** loop, with counters:
- `perf stat -e cache-misses,L1-dcache-load-misses,dTLB-load-misses ./prog` shows where you miss.
- Compare variants (naive vs blocked, AoS vs SoA) and attribute the time to a counter.
This is exactly the methodology your capstone write-up uses to compare your detector to ASan, and that
you'll use to profile your object store.

## Drills
1. Implement naive vs blocked transpose for N=4096; verify equal results; time both; sweep tile sizes
   T ∈ {8,16,32,64,128} and plot time vs T. Where's the sweet spot, and why (relate T² to L1 size)?
2. Run both under `perf stat -e L1-dcache-load-misses`. Does the miss count track the time?
3. Add `__builtin_prefetch` to a linked-list traversal (non-sequential) and measure; then to a
   sequential array scan and measure. Where does it help, where does it hurt?
4. Align a hot 64-byte struct with `alignas(64)` vs not; measure a tight loop over an array of them.

## My summary
