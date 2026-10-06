# Part 4 — The LSM-Tree: The Idea (1996 → today)

> Read the original paper alongside this part: `[ONEIL96]` (32 pages). It is very readable once
> you have Parts 1–3. This part walks you through it, does the arithmetic with you, and then
> shows how today's engines (LevelDB/RocksDB) changed the design and why.

Contents

1. The problem, in the authors' own numbers
2. "Temperature" and the Five Minute Rule — thinking in *cost*, not complexity
3. The two-component LSM-tree: C0 (memory) and C1 (disk)
4. The rolling merge: emptying blocks, filling blocks, never overwriting
5. Why it's cheaper: the cost formula, worked
6. Growing to K+1 components: why the sizes form a geometric series
7. Finds, deletes, updates — deferred work
8. Recovery in the original design
9. What changed: Bigtable's memtable + SSTables + compactions
10. Today's vocabulary: immutable components, leveling vs tiering, partitioning
11. Summary + drills

---

## 1. The problem

O'Neil et al. consider a bank (the TPC-A benchmark) doing 1000 transactions per second, each
writing a 50-byte row to a `History` table. Customers want "recent activity for my account", so
the table needs an index on `Acct-ID || Timestamp` `[ONEIL96 §1]`.

Their numbers for a B-tree index (Example 1.2):

- 20 days × 8 h × 1000 entries/s = **576 000 000 entries**, 16 bytes each = **9.2 GB**, "about 2.3
  million pages needed on the index leaf level".
- Account IDs are random, so "each transaction will require at least one page read from this
  index, and in the steady state a page write as well".
- Those leaf pages are re-touched only ~every 2300 s — far too rarely to stay in memory.
- Result: +2000 random I/Os per second → "an additional 50 disk arms, doubling our disk
  requirements".

**The insight:** index *inserts* are much more common than index *reads* ("most people don't ask
for recent account activity nearly as often as they write a check" `[ONEIL96 §1]`). So optimise
inserts, and make reads merely *acceptable*.

## 2. Thinking in cost: temperature and the Five Minute Rule

This section is why the paper is a systems paper and not just an algorithms paper.

O'Neil's disk model `[ONEIL96 §3.1]`: when you buy a disk you pay for **capacity** and for **I/O
rate** ("disk arms"). Definitions:

```
COST_d  = cost of 1 MB of disk storage            ($1/MB in 1995)
COST_m  = cost of 1 MB of memory                  ($100/MB)
COST_P  = disk-arm cost of 1 random page/second   ($25 per I/O/s)
COST_π  = disk-arm cost of 1 page/second as part of a multi-page block   ($2.5 per I/O/s)
```

Note COST_π / COST_P ≈ 1/10. They derive it from two drives; e.g. a SCSI-2 disk: one random 4 KB
page = 9 ms seek + 5.5 ms rotation + 1.2 ms read = 16 ms, while 64 contiguous pages = 9 + 5.5 + 80
= 95 ms ≈ 1.5 ms/page `[ONEIL96 §3.1]`. Same phenomenon as OSTEP's 200× gap (Part 1 §4) —
the multi-page block amortises the seek and rotation.

**Temperature** of data = H/S = accesses per second per MB. Cold data is limited by capacity, warm
data by disk arms, hot data belongs in memory. The **Five Minute Rule** generalises to: keep a page
in memory if it is referenced more often than every τ seconds, where
τ = (1/pagesize)·(COST_P/COST_m) — with 1995 prices and 4 KB pages, τ ≈ 62.5 s `[ONEIL96 §3.1]`.

The punchline: "One way to express what an LSM-tree achieves is to say that it reduces the actual
disk accesses and thus lowers the effective temperature of the indexed data." `[ONEIL96 §3.1]`

> **Systems-engineer reflex #5:** the right question is not "is it O(log n)?" but "what does each
> operation cost in *the scarce resource* (disk arms, flash erase cycles, cache misses), and what's
> the price of that resource vs memory?" Prices change; the method doesn't.

## 3. The two-component LSM-tree

`[ONEIL96 §2]`:

> "A two component LSM-tree has a smaller component which is entirely memory resident, known as the
> C0 tree (or C0 component), and a larger component which is resident on disk, known as the C1
> tree."

```
              inserts
                 │
   ┌─────────────▼─────────────┐
   │  C0  (memory; AVL / 2-3   │   no I/O to insert
   │       tree, any node size) │
   └─────────────┬─────────────┘
                 │ rolling merge (multi-page block I/O)
   ┌─────────────▼─────────────────────────────────────┐
   │  C1  (disk; B-tree-like, nodes 100% full,          │
   │       leaves packed into 256 KB multi-page blocks)  │
   └────────────────────────────────────────────────────┘
```

The insert path, step by step `[ONEIL96 §2]`:

1. "a log record to recover this insert is first written to the sequential log file in the usual
   way" — **sequential** I/O, the durability anchor.
2. "The index entry … is then inserted into the memory resident C0 tree" — "no I/O cost".
3. Later, entries "migrate out to the C1 tree on disk".
4. "any search for an index entry will look first in C0 and then in C1".

C1's design choices, and the *reason* for each **(derived from `[ONEIL96 §2]` + Part 1)**:

| C1 choice | Reason |
|---|---|
| "nodes 100% full" | no in-place inserts ever happen → no need for free space in nodes; denser = fewer pages |
| leaves "packed together in contiguous multi-page disk blocks" of ~256 KB | the merge reads/writes them sequentially → COST_π instead of COST_P |
| single-page nodes still used for exact-match finds | a point lookup reads one page, not a whole 256 KB block, "to minimize buffering requirements" |
| upper directory levels stay in memory buffers | they're small and hot |

## 4. The rolling merge

`[ONEIL96 §2, §2.1]`, condensed:

- When C0 nears its size threshold, "an ongoing rolling merge process serves to delete some
  contiguous segment of entries from the C0 tree and merge it into the C1 tree on disk."
- A conceptual **cursor** circulates through key space. At the cursor, a multi-page block of C1
  leaves is read into memory — the **emptying block**. Its entries are merged with the C0 entries
  in the same key range; the merged leaves go into a new in-memory **filling block**. When the
  filling block is full, "the block is written to a new free area on disk".
- "Newly merged blocks are written to new disk positions, so that the old blocks will not be
  overwritten and will be available for recovery in case of a crash."
- When the cursor reaches the largest key, "the rolling merge starts again from the smallest
  values".

The name: "The idea of always writing multi-page blocks to new locations was inspired by the
Log-Structured File System devised by Rosenblum and Ousterhout, from which the Log-Structured
Merge-tree takes its name." And an advantage over LFS: "In the LSM-Tree, blocks are totally freed
up on the trailing edge of the rolling merge, so no extra I/O is involved" in reusing space
`[ONEIL96 §2.1]` — LFS had to read partly-live segments to clean them (see `[OSTEP-43]`).

**(derived)** The rolling merge *is* merge sort's merge step (Part 3 §6), run incrementally,
forever, between a small sorted structure in RAM and a big sorted structure on disk.

## 5. Why it's cheaper — the formula, worked

Definitions `[ONEIL96 §3.2]`:

- B-tree insert: read the path (D_e pages not in cache, "typically about 2") + write the leaf back:
  **COST_B-ins = COST_P · (D_e + 1)**  (3.1)
- **M** = "the average number of entries in the C0 tree inserted into each single page leaf node of
  the C1 tree during the rolling merge" = (S_p/S_e) · (S_0 / (S_0 + S_1))  (3.2),
  where S_p = page size, S_e = entry size, S_0, S_1 = component sizes.
- LSM insert: each C1 leaf page is read and written once per merge pass (2 × COST_π) and absorbs
  M new entries: **COST_LSM-ins = 2 · COST_π / M**  (3.3)
- Ratio: **COST_LSM-ins / COST_B-ins = K1 · (COST_π / COST_P) · (1/M)**, K1 = 2/(D_e+1) ≈ 0.67 (3.4)

Their "typical" values: S_1 = 40·S_0 and 200 entries per page → M = 200 · (1/41) ≈ 5 `[ONEIL96 §3.2]`.

Worked **(derived)**: ratio ≈ 0.67 × (1/10) × (1/5) = 0.0134 → an LSM insert costs about
**1/75** of a B-tree insert in disk-arm time. The paper: "Typically the product of the two ratios
will give a cost ratio improvement of nearly two orders of magnitude."

The two separate effects to remember:

1. **Sequential-I/O effect** (COST_π/COST_P): pages move in big contiguous blocks.
2. **Batching effect** (1/M): each page read/written carries *many* new entries, not one.

## 6. More components and the geometric series

The catch in formula (3.2): M grows with S_0/S_1. A huge C1 with a small C0 makes M < 1 — "more
than one C1 tree page must be brought in and out of memory for each entry which is merged in"
and then "we would do better to use a normal B-tree" `[ONEIL96 §3.3]`. Making C0 big costs
expensive memory.

The fix: add intermediate disk components. "an LSM-tree of K+1 components has components C0, C1,
C2, . . ., CK-1 and CK, which are indexed tree structures of increasing size; the C0 component tree
is memory resident and all other components are disk resident … there are asynchronous rolling
merge processes in train between all component pairs (Ci-1, Ci)" `[ONEIL96 §3.3]`.

**Theorem 3.1 (the result everything since is built on):** with the memory size S_0 and the
largest component S_K fixed, the total merge I/O is minimised "when all of the values r_i are
equal to a single constant value r", where r_i = S_i / S_{i−1} `[ONEIL96 §3.4]`. So:

```
S_i = r^i · S_0        (a geometric progression)
S   = S_0 + r·S_0 + r²·S_0 + … + r^K·S_0
```

The survey's summary: "write performance is optimized when the size ratios Ti = |Ci+1|/|Ci|
between all adjacent components are the same. This principle has impacted all subsequent
implementations and improvements of LSM-trees." `[LUO20 §2.1]`

**This is why LevelDB's levels are 10 MB, 100 MB, 1000 MB, …** ("When the combined size of files
in level-L exceeds (10^L) MB" `[LDB-IMPL]`) and why RocksDB's default
`max_bytes_for_level_multiplier = 10` produces targets of 16384, 163840, 1638400, … in their
example `[ROCKS-LEVELED]`. The "10" is the r of Theorem 3.1.

**(derived)** A consequence: the last level's share of the total is
r^K / (1 + r + … + r^K) = r^K·(r−1) / (r^(K+1) − 1) ≈ (r−1)/r for large K. With r = 10 that is
≈ 0.9 — the last level holds ~90% of all data. RocksDB states the
same target: "a stable LSM-tree structure, where 90% of data is stored in the last level"
`[ROCKS-LEVELED]`. The cost: each extra component adds roughly "one extra page I/O per disk
component" to a find `[ONEIL96 §3.3]`.

## 7. Finds, deletes, updates — all deferred

- **Finds**: search C0, then C1, … CK. Early termination is possible when keys are unique and
  found early, or when the query asks only for recent data `[ONEIL96 §2.2]`.
- **Deletes** are *inserts of a marker*: "a delete node entry can be placed in that position …
  The actual delete can be done at a later time during the rolling merge process, when the actual
  index entry is encountered: we say the delete node entry migrates out to larger components during
  merge and annihilates the associated entry when it is encountered. In the meantime, find requests
  must be filtered through delete node entries" `[ONEIL96 §2.3]`. Today this marker is called a
  **tombstone** (LevelDB: "a deletion marker for the key … kept around to hide obsolete values
  present in older sorted tables" `[LDB-IMPL]`).
- **Updates** = delete + insert `[ONEIL96 §2.3]`. (Modern engines simply insert the new version;
  the newest version shadows older ones.)
- **Predicate deletion**: "all index values with timestamps more than 20 days old are to be
  deleted" — dropped during the merge as they're encountered `[ONEIL96 §2.3]`. RocksDB's
  *compaction filter* and *FIFO* compaction are the modern descendants `[ROCKS-OVERVIEW]`.

**(derived)** The LSM-tree turns every mutation into an *append*. That's the core reason the write
path never does random I/O — and the core reason reads must look in multiple places.

## 8. Recovery in the original design

`[ONEIL96 §4.2]`: C0 lives in memory, so a crash loses it. But the *log records* of the inserts are
already on disk (step 1 of §3). Recovery = find the last **checkpoint**, then replay the log from
there. A checkpoint records:

- "The Log Sequence Number, LSN0, of the last inserted indexed row at time T0"
- "The disk addresses of the roots of all components"
- "The location of all merge cursors in the various components"
- "The current information for dynamic allocation of new multi-page blocks."

Because merged blocks are always written to *new* locations, the old blocks referenced by the last
checkpoint are still intact after a crash **(derived)**.

> Compare with LevelDB (Part 6): LSN0 ↔ the WAL/sequence number, "roots of all components" ↔ the
> MANIFEST's list of live files, "dynamic allocation info" ↔ next file number. Same ideas, 15
> years later.

## 9. What changed: Bigtable (2006)

Bigtable gave us today's vocabulary `[BIGTABLE06 §5.3–5.4]`:

- "Updates are committed to a commit log that stores redo records. Of these updates, the recently
  committed ones are stored in memory in a sorted buffer called a **memtable**; the older updates
  are stored in a sequence of **SSTables**."
- **SSTable**: "a persistent, ordered immutable map from keys to values, where both keys and
  values are arbitrary byte strings … each SSTable contains a sequence of blocks (typically each
  block is 64KB in size …). A block index (stored at the end of the SSTable) is used to locate
  blocks" `[BIGTABLE06 §4]`.
- **Minor compaction**: "When the memtable size reaches a threshold, the memtable is frozen, a new
  memtable is created, and the frozen memtable is converted to an SSTable and written to GFS."
- **Merging compaction**: "reads the contents of a few SSTables and the memtable, and writes out a
  new SSTable."
- **Major compaction**: "rewrites all SSTables into exactly one SSTable … SSTables produced by
  non-major compactions can contain special deletion entries that suppress deleted data in older
  SSTables that are still live. A major compaction, on the other hand, produces an SSTable that
  contains no deletion information or deleted data." Bigtable runs them regularly so that "deleted
  data disappears from the system in a timely fashion, which is important for services that store
  sensitive data." (Part 7 returns to this.)
- **Group commit** "is used to improve the throughput of lots of small mutations" — many writers
  share one log write/sync.

LevelDB states it is "similar in spirit to the representation of a single Bigtable tablet"
`[LDB-IMPL]`, and RocksDB "was forked from open source leveldb 1.5" `[ROCKS-OVERVIEW]`.

## 10. Today's vocabulary

From the survey `[LUO20 §2.2]`:

- **Immutable components instead of a rolling merge.** "today's LSM-tree implementations commonly
  exploit the immutability of disk components to simplify concurrency control and recovery.
  Multiple disk components are merged together into a new one without modifying existing
  components. This is different from the rolling merge process proposed by the original LSM-tree."
  And "the originally proposed rolling merge process is not used by today's LSM-based storage
  systems due to its implementation complexity" `[LUO20 §2.1]`.
- **Memory component**: "a concurrent data structure such as a skip-list or a B+-tree".
- **Disk component**: "B+-trees or sorted-string tables (SSTables). An SSTable contains a list of
  data blocks and an index block".
- **Merge policies**, both controlled by a size ratio T:
  - **Leveling**: "each level only maintains one component, but the component at level L is T
    times larger than the component at level L − 1" — optimised for reads/space.
  - **Tiering**: "maintains up to T components per level. When level L is full, its T components
    are merged together into a new component at level L + 1" — optimised for writes. (Tiering
    descends from Jagadish et al.'s "stepped-merge" policy `[LUO20 §2.1]`.)
- **Partitioning**: "range-partition the disk components … into multiple (usually fixed-size)
  small partitions … we use the term SSTable to denote such a partition". Benefits: bounded merge
  time and temp space; skewed or sequential keys need little merging. "only the partitioned
  leveling policy has been fully implemented by industrial LSM-based storage systems, such as
  LevelDB and RocksDB". **(derived)** Partitioning is how the modern engine recovers the "rolling"
  benefit — merging one small key range at a time — without the rolling merge's complexity.

The picture you'll implement (partitioned leveling, LevelDB-style):

```
memtable (skip list) ──flush──► L0: [a..z] [c..q] [b..y]      ← files may overlap (fresh flushes)
                                 │ compaction (merge all overlapping L0 + overlapping L1)
                                 ▼
                       L1 (≈10 MB):  [a..f][g..m][n..z]         ← one sorted run, no overlaps
                                 │ pick one L1 file + overlapping L2 files
                                 ▼
                       L2 (≈100 MB): [a..b][c..e][f..h] … [x..z]
                       …
```

## 11. Summary

| 1996 term | Modern term | Purpose |
|---|---|---|
| sequential log of inserts | WAL / commit log | durability of C0 |
| C0 tree (AVL / 2-3) | memtable (skip list) | absorb writes in RAM |
| C1…CK (100% full B-trees in multi-page blocks) | SSTables organised in levels | sorted, immutable, sequentially written |
| rolling merge | compaction (minor = flush, merging, major) | move data down, drop garbage |
| delete node entry | tombstone | defer deletes |
| checkpoint (LSN0, roots, cursors) | MANIFEST + CURRENT + WAL | recovery |
| equal ratio r between components (Thm 3.1) | level size multiplier (e.g. 10) | minimise merge I/O |

## Drills

1. Recompute formula (3.4) for a modern NVMe SSD. Look up (and cite!) a random-4K-write IOPS and
   sequential MB/s figure from a vendor datasheet, estimate COST_π/COST_P as a time ratio, choose
   S_e = 100 B, S_p = 4 KiB, S_1 = 10·S_0. Is the LSM still worth it? What changes?
2. With r = 10 and S_0 = 64 MiB, how many levels do you need for 1 TiB? What fraction of data is
   in the last level? Show the series.
3. Read `[ONEIL96 §2.3]` on *long-latency finds* and explain in your own words how a "find note
   entry" can answer a query "for free" during merges.
4. Explain to an imaginary colleague, in 5 sentences and without the word "LSM", why appending is
   cheaper than updating in place on (a) an HDD and (b) an SSD. Cite Part 1.

## My summary

_(write 5 lines here)_
