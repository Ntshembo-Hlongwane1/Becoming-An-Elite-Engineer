# 08 — Synthesis: Retrieval & Storage, End to End

> The capstone. Everything from docs 01–07 assembled into the two paths that define a search
> engine — the **write path** (document → durable, searchable index) and the **read path**
> (query string → ranked results) — plus the **index lifecycle**, a **staged build roadmap**
> that turns your current engine into this one, and the honest boundary between the
> single-node core you can build and the distributed ceiling beyond it. Covers your items (i)
> and (h), and ties off the series.

---

## 1. The write path, end to end

```
 raw document (a data/*.txt product record)
    │
 [ANALYZE]  parse fields; tokenize/normalize text (your Lexer, reused)          doc 05 §2
    │        → text fields → post-analysis terms; keyword fields (Brand/SKU) verbatim;
    │          numeric fields (Price/Stock/Rating) → typed values
    ▼
 [BUFFER]   append to in-RAM indexing buffer; assign a dense uint32 docID        doc 03 §2 / 04 §9
    │        (translog append+fsync here IF you support durable live writes)      doc 03 §5
    ▼
 [FLUSH]    serialize buffer → a NEW immutable segment:                          doc 02, 03 §2
    │        · term dictionary (B+Tree→FST) : term → postings offset             doc 02 §3
    │        · postings (.doc): sorted docIDs, delta+block(FOR/PForDelta), skips,
    │          per-block impacts, freqs                                          doc 04
    │        · points (BKD / attribute B+Tree): price/stock/rating for ranges    doc 02, 06 §6
    │        · doc values (columnar): for sort/filter                            doc 01 §7
    │        · stored fields (.stored): original record for the fetch phase      doc 05 §7
    │        · norms: quantized field lengths for BM25                           doc 06 §3
    │        · checksums + magic/version per file                                doc 02 §2/§4
    ▼
 [REFRESH]  open readers over the new segment → SEARCHABLE (NRT lag)             doc 03 §3
    ▼
 [COMMIT]   fsync segments + write new manifest/segments_N (atomic swap)          doc 02 §4 / 03 §4
    ▼
 [MERGE]    background: consolidate small segments, purge tombstones,            doc 03 §7 / 07 §4-5
             publish new immutable set via atomic swap; old files refcount-freed
```

Read it top-to-bottom and notice: **every stage is a doc.** The write path is the series in
order.

---

## 2. The read path, end to end

```
 query string ("in-stock bread under R20")
    │
 [ANALYZE]   same analyzer as indexing (byte-for-byte symmetry)                 doc 05 §2
    ▼
 [PARSE]     → query tree: BOOL{ MUST text:bread; FILTER price<2000; FILTER stock:in }  doc 05 §3
    ▼
 [REWRITE]   prefix→terms (FST), range→point-iterator/bitset                    doc 05 §4
    ▼
 [WEIGHT]    idf(bread), avgdl, boosts; plan per-segment scorers                doc 05 §4 / 06 §3
    ▼
 [PER-SEGMENT] (parallel across segments — the real fan-out)                    doc 05 §5
    │  candidates = leapfrog-AND( postings(bread) , filterBitset(price<2000 ∧ stock) )
    │  DocIdSetIterators + skip lists skip blocks                               doc 04 §6 / 05 §5
    │  BM25 score survivors; BlockMax WAND skips blocks below heap threshold     doc 06 §3-5
    │  feed a size-K min-heap (top-K)                                            doc 06 §4
    ▼
 [MERGE]     merge per-segment (per-shard) top-Ks → global top-K docIDs         doc 05 §7 / 06
    ▼
 [RESCORE]   (optional) re-rank top-N by business rules (rating, stock, margin) doc 06 §7
    ▼
 [FETCH]     read stored fields for the final K → build response                doc 05 §7
    ▼
 ranked results → user
```

The read path is the write path's mirror: the encodings you *wrote* (postings, skips,
impacts, norms, stored fields) are precisely what the reader *exploits* to do less work.
**Storage and retrieval are co-designed** — you can't understand one without the other, which
is why this series interleaved them.

---

## 3. The index lifecycle (item h, whole)

```
 BIRTH      cold build: pipeline fills buffer → flush first segment → commit      (your cold start)
   │                                                                              startup-01..05
 GROWTH     incremental adds → more segments; refresh makes them visible          doc 03 §2-3
   │
 MAINTAIN   background merge consolidates; tombstones purged; caches evict        doc 03 §7 / 07
   │
 DURABILITY commits checkpoint the manifest; translog covers the gap             doc 03 §4-5
   │
 SERVE      point-in-time readers answer queries against immutable snapshots      doc 03 §8
   │
 REOPEN     new readers pick up new segments cheaply (reuse unchanged ones)       doc 03 §8
   │
 RETIRE     merged-away segments' files deleted on last-reader release (refcount) doc 03 §8 / 07 §5
```

Your `startup` series was **birth**; this series is the rest of the life. Together they cover
an index from first build to steady-state operation.

---

## 4. From your engine today → this engine (staged roadmap)

Each stage leaves you with a **runnable, testable** engine — never a big-bang rewrite. This is
your build order across the whole series.

| Stage | What you build | Unlocks | Docs |
|---|---|---|---|
| 0 (now) | in-RAM `unordered_map` index, unranked | baseline | — |
| 1 | **dense uint32 docIDs** + `docID→filepath` table; postings become ints | compression, fetch phase | 04 §9 |
| 2 | **sorted postings + delta + VByte**, serialized to one **segment file** | on-disk index, warm start | 02, 04 §3-4 |
| 3 | **manifest + atomic commit**; open = read manifest, mmap segment | point-in-time open, durability | 02 §4, 03 §4 |
| 4 | **BM25 + top-K min-heap**; reuse Lexer as shared analyzer | *ranked* results | 05 §2, 06 §3-4 |
| 5 | **blocks(128) + FOR + skip lists**; `advance()` | fast intersection | 04 §5-6 |
| 6 | **attribute index (B+Tree/points)** on price/stock/rating; filters + bitset cache | inventory range/filter search | 02, 05 §6, 06 §6 |
| 7 | **per-block impacts + WAND / BlockMax WAND** | skip most scoring | 04 §7, 06 §5 |
| 8 | **Trie/FST prefix** (or B+Tree range-scan) → autocomplete | prefix/suggest | 02 §3, 05 §4 |
| 9 | **multiple segments + background merge** (`MergeScheduler` subsystem) | scale, NRT, compaction | 03 §7, 07 |
| 10 | **rescoring** (business rules), **sort modes** | commerce-grade ranking | 06 §6-7 |

Stages 1–4 alone give you a **persistent, ranked, from-scratch search engine** — already
beyond what most people ever build. 5–10 make it frontier-shaped.

> **Do them in order.** Each depends on the last (you can't WAND without impacts, can't skip
> without blocks, can't rank without docIDs+norms). Resist jumping to stage 7 because it's the
> coolest — you'll be debugging four layers at once. This is the same discipline as your
> `startup-05` build order, scaled to the whole engine.

---

## 5. The whole architecture on one page

```
                    ┌───────────────────────── CONTROL PLANE (your Kernal) ─────────────────────────┐
                    │  Register / Init / Start / Stop  ·  boot decision (warm vs cold, startup docs) │
                    └───────────────┬───────────────────────────────────────────────┬───────────────┘
                                    │                                               │
        ┌───────────────────────────▼──────────────┐                 ┌──────────────▼───────────────────┐
        │ WRITE (data plane)                        │                 │ READ (data plane)                 │
        │ DirReader→Lexer(analyze)→buffer→FLUSH→seg │                 │ analyze→parse→rewrite→weight      │
        │           │                               │  immutable      │        │                          │
        │  RefreshWorker  FlushWorker  MergeSched.  │  segments  ◄────┤  per-segment scorers (DocIdSet-   │
        │           │         │            │        │  (mmap +        │  Iterators, skips, BM25, BlockMax │
        │           └─────────┴────────────┘        │  page cache)    │  WAND) → top-K heap → merge →     │
        │        background workers (doc 07)         │                 │  rescore → FETCH stored fields    │
        └───────────────────────────────────────────┘                 └───────────────────────────────────┘
                                    │                                               │
                    ┌───────────────▼───────────────────────────────────────────────▼───────────────┐
                    │ STORAGE: segments = files of fixed pages · term dict · postings(delta/block/    │
                    │ skip/impacts) · points/BKD · doc values · stored · norms · manifest/commit      │
                    │ addressed by page-id/offset · accessed via mmap + OS page cache (docs 01-02)    │
                    └───────────────────────────────────────────────────────────────────────────────┘
```

Notice the control plane / data plane split is *your* `Kernal`/`Subsystem` design (startup
series), the background workers are *your* threaded subsystems (doc 07), and the queues
between write stages are *your* `RingBuffer`s. **The frontier architecture is your current
architecture, filled in with the storage and retrieval mechanisms from this series.**

---

## 6. The honest scope line: core vs ceiling

**What you can genuinely build (single-node core) — and should:**
- immutable segments + manifest/commit + point-in-time readers
- compressed on-disk postings (delta/VByte→FOR→skip lists→impacts)
- an ordered term dictionary (B+Tree → FST as a stretch)
- BKD/B+Tree attribute indexes + doc values for inventory filters/sort
- BM25 + top-K heap + WAND/BlockMax WAND
- Trie/FST prefix suggester
- background flush/refresh/merge as `Kernal` subsystems, backpressured
- rescoring for commerce ranking

That is a **real, frontier-faithful search engine**. It's a huge amount of depth and it's
all reachable.

**The distributed ceiling (Elasticsearch layer) — understand, don't build (yet):**
- **sharding** (each shard = an independent Lucene index) for horizontal scale
- **replicas** for availability + read throughput
- **routing** (which shard owns a doc) and **cluster state** (who owns what)
- **scatter-gather across nodes** — the *same* query-then-fetch (doc 05 §7), just over the
  network with node failures, retries, and partial results
- consensus/coordination for cluster membership

It's the *same patterns* (immutable segments, query-then-fetch, merge) scaled across machines,
plus distributed-systems concerns (partial failure, consistency, network) that are a whole
separate field. Treat it as **the next project**, not this one. Building the single-node core
first is exactly how the people who built ES learned it — Lucene came before Elasticsearch.

---

## 7. The five ideas to carry out of this entire series

1. **The memory hierarchy is the boss.** Every mechanism is "move fewer, closer bytes." (01)
2. **Immutability is the master key.** It gives lock-free reads, trivial cache invalidation,
   safe background merging, and mmap-able storage. Nearly every hard problem was *designed
   away* by never mutating. (02–03, 07)
3. **Co-design storage and retrieval.** The reader is fast only because the writer stored
   skip lists and per-block impacts for it. (04–06)
4. **Do less work: prune, skip, defer.** Skip lists skip pages; BlockMax WAND skips scoring;
   query-then-fetch defers document reads; rescoring defers expensive ranking to a tiny
   survivor set. (04–06)
5. **You already own the substrate.** Control plane, subsystems, threads, queues,
   backpressure, atomic swaps — your engine's bones are the frontier engine's bones. (07, and
   your startup series)

---

## 8. Final self-check (the whole series)

If you can do these from a blank page, you have the depth you asked for:

1. Draw the write path and the read path, labeling which doc each stage came from.
2. Explain, for one query on your `data/`, exactly which bytes get read from disk and which
   get skipped — and *why* each skip is safe.
3. Justify immutability by naming four distinct problems it solves.
4. Trace a document's life from `add()` through refresh, commit, and merge to retirement.
5. Explain why compression makes reads faster and why block encoding beats per-value VByte.
6. Give the staged build order from your current `unordered_map` to a WAND-pruned, on-disk,
   segment-based engine — and say what each stage lets you test.
7. Draw the line between the single-node core and the distributed ceiling, and say why the
   ceiling is "the same patterns plus partial failure."

---

## 9. Where to go next

- **Build stage 1–4** (docIDs → on-disk segment → commit → BM25). That's a complete, ranked,
  persistent engine and the highest-leverage milestone in the whole roadmap.
- Keep the **series index** (`00-index.md`) open as your map; each stage points back to the
  doc that explains it.
- When you hit a wall, the wall is usually the memory hierarchy (01) or immutability (03)
  reasserting itself — return to those two docs first.

The docs have done their job. The engine is yours to build — one testable stage at a time.
