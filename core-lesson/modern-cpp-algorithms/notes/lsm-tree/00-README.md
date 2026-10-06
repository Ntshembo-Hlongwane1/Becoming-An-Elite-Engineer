# LSM-Tree — A Build-Up Lesson From Zero (Systems-Engineer Edition)

> Goal: by the end you can explain — and build — a Log-Structured Merge-tree **down to the byte
> on disk and the cache line in RAM**, and you can reason about it the way a storage engineer
> (and a security researcher) does.

## Ground rules for these notes

1. **Zero assumed knowledge of concepts.** You can write C++; nothing else is assumed. Words like
   *page*, *alignment*, *atomic*, *fsync*, *endianness*, *arena* are all taught before use.
2. **Every claim is sourced.** Tags like `[OSTEP-37]` point to `SOURCES.md`. Things I work out
   are marked **(derived)**; things I ran are marked **(measured)**.
3. **Your two books are the spine.** `[ALGO §...]` = *Modern C++ Algorithms*,
   `[MEM §...]` = *Advanced Memory Management*. When a book snippet has a real bug, I show the
   bug *with evidence* — finding those is part of becoming a systems engineer.

## Reading order

Each part depends on the ones before it. Do not skip Part 1 and 2 — the LSM-tree only makes
sense as an *answer to the hardware*, and you cannot build one safely without the C++ memory
model.

| # | File | What you learn | Book chapters it builds on |
|---|---|---|---|
| 1 | `01-the-machine-from-zero.md` | bytes, pages, caches, HDD/SSD physics, page cache, syscalls, durability, `fsync`, checksums | `[ALGO §11.1, §22.1]`, `[MEM §9.3]` |
| 2 | `02-cpp-memory-from-zero.md` | object layout, alignment, padding, endianness, aliasing, serialization, varints, heap vs arena, placement new, RAII, threads, atomics, memory ordering | `[MEM ch.2, 4, 14, 15, 17]`, `[ALGO §3.2, §22.1, §23.1]` |
| 3 | `03-dsa-building-blocks.md` | sorted runs, comparators, balanced trees, **skip lists**, B-trees (and why they hurt on writes), merge / k-way merge, **Bloom filters**, CRCs | `[ALGO ch.6, 9, 10, 11, 17, 20]` |
| 4 | `04-lsm-the-idea.md` | the *problem* the LSM-tree solves, the original 1996 design, components, rolling merge, why sizes grow geometrically | — (new) |
| 5 | `05-lsm-the-engine.md` | a modern LSM end-to-end: WAL → memtable → flush → SSTables → levels → compaction; reads, deletes, sequence numbers, snapshots, amplification math, stalls, recovery | — (new) |
| 6 | `06-on-disk-layout-and-page-ordering.md` | **byte-level** formats: WAL blocks, SSTable data/index/filter blocks, footer, magic numbers, MANIFEST, CURRENT; *every* kind of "ordering" that matters on storage; crash-safe write ordering | `[ALGO §11.1–11.2]` |
| 7 | `07-security-researcher-view.md` | the LSM as an attack surface: parsers of on-disk bytes, checksums ≠ authentication, tombstone resurrection, data remanence, write-stall DoS | `[MEM §5]` |
| 8 | `08-glossary.md` | every term, one line each | — |

Then: `../../exercises/lsm-tree/` — **Exercise 1** (in-memory LSM) and **Exercise 2**
(on-disk LSM). Do Exercise 1 after Part 5 and Exercise 2 after Part 6.

## How to study each part

1. Read once straight through for the story.
2. Re-read with the source open next to it (links in `SOURCES.md`). Verify at least one claim per
   section yourself. This is the habit that separates "I read about it" from "I know it".
3. Do the **Drills** at the end of each part — small programs or calculations, 5–30 minutes each.
4. Write a 5-line summary in your own words at the bottom of each file (there is a
   `## My summary` heading waiting for you).

## The one-paragraph picture (you will understand every word of this by Part 5)

An LSM-tree turns random writes into sequential writes. A write is first **appended** to a
**write-ahead log** on disk (sequential, crash-safe) and inserted into a sorted in-memory
structure, the **memtable** (usually a **skip list**). When the memtable is full it is frozen and
written out, in key order, as an immutable **sorted string table (SSTable)** file. SSTables are
organised into **levels** of exponentially growing size; a background **compaction** process
**merge-sorts** overlapping files into the next level, discarding overwritten values and
deletion markers (**tombstones**). A read checks the memtable, then the SSTables from newest to
oldest, using small in-memory **indexes** and **Bloom filters** to touch at most a few disk
**blocks**. A tiny **MANIFEST** log plus an atomically-renamed **CURRENT** file record which
files make up the tree, so the whole thing can be rebuilt after a crash.
(Paraphrasing `[LDB-IMPL]`, `[ROCKS-OVERVIEW]`, `[LUO20 §2.2]`.)
