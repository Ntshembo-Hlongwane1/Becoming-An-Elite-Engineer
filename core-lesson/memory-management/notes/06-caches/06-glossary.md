# Lesson 6 — Glossary

| Term | One line | § |
|---|---|---|
| memory hierarchy | registers → L1 → L2 → L3 → DRAM, faster→slower/smaller→larger | 6.1 |
| cache | small fast memory caching the level below it | 6.1 |
| cache line | unit of transfer between levels; 64 B here | 6.1 |
| latency (L1/L3/DRAM) | ~1 ns / ~15 ns / ~60-100 ns (order of magnitude) | 6.1 |
| set-associative | an address maps to a small set of line slots; LRU-ish eviction | 6.1 |
| conflict miss | many addresses map to the same set and evict each other | 6.1 |
| spatial locality | nearby addresses accessed together (a line serves many) | 6.2 |
| temporal locality | same address reused while still cached | 6.2 |
| row- vs column-major | traverse in storage order (4.8× here) | 6.2 |
| AoS / SoA | array-of-structs vs struct-of-arrays; line utilisation | 6.2 |
| blocking / tiling | process cache-sized tiles so loaded data is fully reused | 6.3 |
| prefetch | pull lines ahead of use (hardware, or __builtin_prefetch) | 6.3 |
| cache-line alignment | keep a hot datum within one line (alignas(64)) | 6.3 |
| cache coherence (MESI) | keeps per-core cached copies of a line agreed | 6.4 |
| false sharing | threads' independent vars share a line → ping-pong (~5× here) | 6.4 |
| hardware_destructive_interference_size | std name for the false-sharing pad (64) | 6.4 |
| true sharing cost | even correctly-shared lines ping-pong; shard to fix | 6.4 |
| timing side channel | cache hit vs miss is measurable → access patterns leak secrets | 6.5 |
| constant-time code | no secret-dependent branches or memory indices | 6.5 |
