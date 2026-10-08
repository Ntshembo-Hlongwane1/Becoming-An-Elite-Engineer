# 6.1 — The Memory Hierarchy

## 1. Why there's a hierarchy at all

A modern CPU core runs at ~3–4 GHz: one clock is ~0.3 ns. DRAM takes ~50–100 ns to answer — hundreds
of clocks. If every memory access waited for DRAM, the CPU would idle ~99% of the time. The fix is a
**hierarchy** of progressively larger, slower memories, each caching the level below it, so the common
case is served from something fast `[MEM §9.3]`, `[ALGO §22.1.1]`, `[MCCP §2.1]`.

## 2. The levels (measured on your VM)

Read straight from `/sys/.../cpu0/cache` **(measured)**:

```
L1 Data        32 KiB   line 64 B    (per core)
L1 Instruction 32 KiB   line 64 B
L2 Unified    256 KiB   line 64 B    (per core)
L3 Unified   9216 KiB   line 64 B    (shared across cores)
```

Plus, above L1, a handful of **registers** (the only storage the CPU computes on directly), and below
L3, **DRAM** (your GiBs of RAM) and then swap/disk. Approximate latencies (order of magnitude, from
the standard references; not measured here):

| Level | Size (yours) | Latency ~ |
|---|---|---|
| register | ~16 × 8 B | 0 (compute directly) |
| L1 | 32 KiB | ~1 ns (a few cycles) |
| L2 | 256 KiB | ~4 ns |
| L3 | 9 MiB | ~15 ns |
| DRAM | GiBs | ~60–100 ns |

The jumps are the point: L1→DRAM is ~100×. "Is my working set in cache?" often matters more than
"how many operations do I do."

## 3. The cache line is the unit

Caches don't move bytes; they move **cache lines** — 64 bytes on essentially all current x86/ARM
**(measured: `coherency_line_size` = 64; `[ALGO §22.1.1]`: "Typically 64 bytes")**. Touch one byte and
the whole 64-byte line is pulled into cache. Consequences used in the rest of the lesson:
- Accessing the *next* bytes after one you just touched is nearly free — they came along in the line
  (**spatial locality**, §6.2).
- A data structure's useful bytes per line decide how much cache you waste (AoS vs SoA, §6.2).
- Two variables in one line are a unit to the coherence protocol — the root of false sharing (§6.4).
- Aligning a 64-byte structure to a line boundary keeps it in exactly one line, not split across two
  (alignment lesson Part 1 §11; `std::hardware_destructive_interference_size` = 64, measured earlier).

## 4. How caching decides what to keep

A cache is finite, so it evicts. Caches are **set-associative**: an address maps to a *set* of a few
line slots (its "ways"); a new line evicts an old one in that set (roughly least-recently-used). You
don't control it directly, but you influence it: sequential access reuses lines and sets predictably;
large power-of-two strides can map many addresses to the *same* set and thrash it (cache "conflict
misses"). The TLB (Lesson 4.1 §4) is a parallel cache for *translations* with the same hit/miss story.

## 5. What you can actually do
You can't make DRAM faster, but you can keep your working set small and your accesses local so you hit
L1/L2 instead of DRAM. The next sections are the three levers: **locality** (§6.2), **blocking +
prefetch** (§6.3), and **avoiding coherence stalls** (§6.4). The exercise measures each on your
machine.

## Drills
1. Print your cache sizes/line from `/sys/devices/system/cpu/cpu0/cache/index*/`. Compute how many
   `int`s fit in L1 (32 KiB / 4).
2. Make an array that fits in L1 (e.g. 4 K ints) and one that fits only in L3 (e.g. 2 M ints) and one
   that exceeds L3. Sum each repeatedly; compare per-element time. Map the jumps to the table in §2.
3. Explain, using §3, why reading `a[i]` then `a[i+1]` is much cheaper than reading `a[i]` then
   `a[i+100000]`.

## My summary
