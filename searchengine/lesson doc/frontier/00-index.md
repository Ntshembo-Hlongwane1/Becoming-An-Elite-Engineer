# Frontier Search Engine Internals — Low-Level Series

> **Goal:** understand how Lucene / Elasticsearch actually work *on the metal* — bytes,
> pages, cache lines, page faults, encoding schemes, background threads — and map every
> mechanism onto what you'll implement in your own inventory search engine in C++.
>
> **Optimised for depth, not breadth.** We go down to the hardware and the file format, not
> up to feature lists. Where a frontier engine uses an advanced structure (FST, BKD, PFOR),
> we explain it fully *and* give the honest simpler version you'd build first.
>
> **Teaching contract (unchanged):** concepts, mechanisms, and design questions — not
> finished feature code for your engine. Illustrative snippets teach a technique; the
> integration is yours to write.

---

## The reference architecture, in one paragraph

A Lucene index is a set of **immutable segments**. Each segment is a self-contained
inverted index stored as a handful of files, laid out in **fixed-size pages** and accessed
via **mmap + the OS page cache**. Documents are added by flushing new segments; deletes are
**tombstones**; **background threads** refresh (make new segments searchable ~1×/sec),
flush (buffer→disk), and **merge** (consolidate small segments, purge tombstones). A query
is **rewritten** (prefixes expand via an ordered term structure), executed **per segment**
as **set operations over compressed posting lists** (delta + block encoded, with skip data
and per-block max-score "impacts"), scored with **BM25**, and reduced to the **top-K** via a
bounded min-heap using **BlockMax WAND** dynamic pruning; segment/shard results merge, then
documents are **fetched**. Everything about this design serves one master: the **memory
hierarchy**.

Every doc below expands one clause of that paragraph.

---

## The series

| Doc | Title | One-line hook |
|---|---|---|
| [01](01-memory-hierarchy.md) | The Memory Hierarchy | Why every design choice is a bet about which layer the data lives in |
| [02](02-storage-layout.md) | Storage Layout | Pages, files, offsets, mmap, OS page cache vs DB buffer pools |
| 03 | Segment Architecture | Immutability, flush/refresh/commit/merge, tombstones, NRT, durability |
| 04 | Posting Lists, Deep | How postings are stored, compressed (delta/FOR/PFOR), skip lists, impacts |
| 05 | Query Lifecycle | Parse → rewrite → weight/scorer → per-segment → collect → merge → fetch |
| 06 | Ranking Pipeline | Candidate generation, BM25, Top-K heap, WAND / BlockMax WAND, rescoring |
| 07 | Background Workers | Merge/refresh/flush threads, merge policy, cache eviction, concurrency |
| 08 | Synthesis | The full read path & write path, mapped to your inventory engine |
| [09](09-unified-operational-flow.md) | Unified Operational Flow (Startup × Frontier) | Reconciles your `startup` boot changes with the frontier changes into one step-by-step flow, with a "why" for each seam |

## Prerequisites / companions
- Your earlier series: `startup-01..05` (warm/cold start, immutability, happens-before),
  `ownership-and-lifecycle-*`, `concurrency.md`, `backpressure-*`, `cpp-file-io.md`,
  `complexity-and-measurement.md`, `performance*`.
- These docs assume that C++ background and build on it.

## How to read
Front-to-back the first time — the dependency order is real. Doc 04 (postings) leans on 01
(memory hierarchy) and 02 (pages); doc 06 (ranking) leans on 04 (impacts/skip data). Don't
skip ahead.
