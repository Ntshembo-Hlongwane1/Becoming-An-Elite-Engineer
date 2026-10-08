# 6.2 — Locality and Cache Lines

## 1. The two localities

`[MEM §9.3]` names them:
- **Spatial locality:** "Accessing memory locations that are physically close together." A cache line
  (§6.1) brings neighbours along, so touching consecutive addresses is nearly free.
- **Temporal locality:** "Re-accessing the same memory locations multiple times in a short period."
  The data is still in cache the second time.

`[ALGO §22.1.1]` adds the same two plus the cache-line point. Code that has both runs from L1; code
with neither misses to DRAM constantly.

## 2. Measured: row-major vs column-major (4.8×)

A 2048×2048 `int` matrix stored row-major (`m[r*N + c]`), summed two ways **(measured)**:

```
sum row-major: 1.9 ms    col-major: 8.9 ms    (ratio 4.8x)   [same sum both ways]
```

Both do the identical 4M additions — same big-O, same result. The 4.8× is pure locality:
- **Row-major traversal** (`for r: for c: m[r*N+c]`) walks consecutive addresses. Each 64-byte line
  holds 16 `int`s, so one cache miss serves 16 accesses — 15 of 16 are line hits **(derived)**.
- **Column-major traversal** (`for c: for r: m[r*N+c]`) jumps `N*4 = 8192` bytes each step, a new
  line (and often a new page/TLB entry) every single access — ~16× more line fetches, and the lines
  it pulled are evicted before the next column needs them. Hence ~5× slower in practice.

Lesson: **traverse memory in the order it's laid out.** For a row-major matrix, make the row index the
inner loop. This single habit is one of the highest-leverage performance rules in systems code.

## 3. AoS vs SoA — bytes per line that you actually use

`[ALGO §22.1.2–22.1.6]` contrasts two layouts for a set of records:

```cpp
struct Particle { float x,y,z, vx,vy,vz; };   // AoS: array of structures
std::vector<Particle> aos;                     // x,y,z,vx,vy,vz, x,y,z,...

struct Particles { std::vector<float> x,y,z,vx,vy,vz; };  // SoA: structure of arrays
```

If a pass only needs positions (`x,y,z`), then:
- **AoS**: each 64-byte line holds ~2.6 particles' *all six* floats, but you use only 3 of 6 — **half
  the line is wasted**, so you fetch ~2× the memory you need.
- **SoA**: the `x` array is contiguous; every byte in a line is an `x` you use — full line utilisation,
  and it vectorises (`[ALGO §22.1.6]`: "SoA layout allows use of std::simd").

`[ALGO §22.1.6]` lists the techniques: align hot data (`alignas(64)`), prefetch (§6.3), SIMD-friendly
SoA, memory pools for contiguity, and avoid false sharing (§6.4). AoS is fine when you touch whole
records together; SoA wins when you sweep one field across many records. Choosing is a cache decision.

## 4. The cache line explains earlier lessons
- The alignment lesson's `alignas(64)` and `hardware_destructive_interference_size` (64) were about
  keeping a datum within one line and off a neighbour's.
- A `struct` with its hot fields grouped into the first 64 bytes keeps them in one line; scattering
  them across a big object costs extra line fetches per access.
- Padding (alignment lesson Part 1 §13) that pushes a hot field past a line boundary silently doubles
  its fetch cost.

## Drills
1. Reproduce §2. Try N = 1024, 4096; does the ratio change? Explain via working-set vs cache size (§6.1).
2. Implement a particle update both AoS and SoA for 1M particles, updating only positions; time both.
   Which wins and by how much? Relate to line utilisation.
3. Add a cold `char notes[128]` field in the middle of a hot struct and measure the slowdown of a
   loop over the hot fields. Move `notes` to the end; re-measure.
4. Why does column-major traversal also stress the **TLB** (Lesson 4.1 §4), not just the cache?

## My summary
