# 01 — The Memory Hierarchy

> **The thesis of the entire series:** a search engine is a machine for **keeping the data
> you need as high up the memory hierarchy as possible, and moving it in the granularity
> each layer prefers.** Postings compression, block encoding, skip lists, mmap, columnar
> doc values, immutable segments — *all of it* exists to win this one game. If you don't
> internalize the hierarchy first, the rest of the series looks like a bag of tricks. It
> isn't. It's one idea applied relentlessly.

---

## 1. The hierarchy, with real numbers

Modern hardware is a stack of caches, each ~10–100× slower but ~10–1000× bigger than the
one above it. The numbers below are order-of-magnitude (they vary by CPU/SSD), but the
*ratios* are what matter and they've held for years:

```
        │ Typical latency │ Size            │ Moved in units of
────────┼─────────────────┼─────────────────┼────────────────────────────
 CPU reg│  ~0.3 ns        │ ~KB (few hundred│ a register (8 bytes)
 L1     │  ~1   ns        │ ~32–64 KB / core│ cache line (64 bytes)
 L2     │  ~4   ns        │ ~256KB–1MB /core│ cache line (64 bytes)
 L3     │  ~10–40 ns      │ ~8–64 MB shared │ cache line (64 bytes)
 RAM    │  ~60–100 ns     │ ~GBs            │ cache line / OS page (4 KB)
 SSD    │  ~16–150 µs     │ ~TBs            │ page / block (4 KB, up to 128KB+)
 HDD    │  ~5–10 ms       │ ~TBs            │ block + seek (whole-arm move)
 Network│  ~0.5 ms LAN+   │ ∞               │ packet / RPC
```

Re-scale those so a human can feel them. Pretend L1 = **1 second**:

```
 L1  cache hit ........ 1 second
 L3  cache hit ........ ~30 seconds
 RAM ................. ~1.5 minutes
 SSD random read ..... ~1.5 DAYS
 HDD seek ............ ~4 MONTHS
```

**Reading one byte from SSD instead of L1 is the difference between "a second" and "a
long weekend."** This is not a rounding error you optimize away later — it is the dominant
force in the design. Every frontier-engine mechanism is a move to turn a "day" into a
"minute."

> **Systems mantra #1:** *The fastest I/O is the I/O you don't do.* Compression, skip
> lists, and dynamic pruning are all forms of "don't read that."

---

## 2. Two kinds of locality — the levers you actually pull

The hardware doesn't give you those top-of-stack speeds for free. It gives them to you
**only if your access pattern cooperates.** Two patterns:

- **Temporal locality** — if you touch a byte, you're likely to touch it again soon. The
  caches bet on this by *keeping* recently-used data. (Your hot terms, your top products.)
- **Spatial locality** — if you touch a byte, you're likely to touch its *neighbors* soon.
  The hardware bets on this by moving data in **blocks**, never single bytes:
  - RAM→cache moves a whole **64-byte cache line**, not your 1 byte.
  - Disk→RAM moves a whole **page** (typically 4 KB), not your 1 byte.

This is the deepest practical consequence in the series: **the unit of transfer is a block,
so the real question is never "how many bytes do I need?" but "how many *blocks* do I
touch, and are they contiguous?"**

```
 You asked for 1 byte at offset 5000.
 The CPU brought bytes 4992..5055 (the 64-byte line containing it).
 The OS brought bytes 4096..8191 (the 4 KB page containing it).
 → If your next read is offset 5001, it's already in cache/RAM: ~free.
 → If your next read is offset 900000, everything you just paid for is wasted.
```

**Sequential access is dramatically faster than random access** — not because "sequential
is nice" but because sequential *reuses blocks you already paid to fetch*, and lets the
hardware **prefetch** the next block before you ask. Random access pays full freight every
time and defeats prefetching.

> **This single fact explains posting-list design (doc 04):** postings are stored as long
> *contiguous, sequential* runs precisely so that scanning them is prefetch-friendly
> block-streaming, not random pointer-chasing. A linked list of postings would be
> correct and catastrophically slow.

---

## 3. Virtual memory, pages, page faults, and the TLB (the low-level plumbing)

You need this to understand mmap (doc 02) and why "a page" is the atom of storage.

- Your program sees **virtual addresses**, not physical RAM. The OS + CPU translate virtual
  → physical in units of **pages** (4 KB default on x86-64).
- The mapping lives in **page tables**. Translating on every access would be slow, so the
  CPU caches recent translations in the **TLB** (Translation Lookaside Buffer). A **TLB
  miss** is a real cost; touching fewer, denser pages keeps more of your working set
  TLB-resident. (This is one reason huge, sparse data structures are slow beyond just cache
  misses.)
- When you touch a virtual page that isn't in RAM, the CPU raises a **page fault**; the OS
  pauses your thread, fetches the page from disk into RAM, updates the page table, and
  resumes you. **A page fault is a hidden, synchronous disk read disguised as a memory
  access.** (This is the double edge of mmap — doc 02.)

```
 load byte @ virtual addr
      │
      ▼
 TLB hit? ──yes──► have physical page frame ──► read from RAM/cache
      │no
      ▼
 walk page tables
      │
 page present in RAM? ──yes──► fill TLB, read
      │no
      ▼
 PAGE FAULT → OS reads 4KB page from disk → thread stalls µs..ms → resume
```

> **Your C++ hook:** when you `mmap` an index file and iterate a posting list, the first
> touch of each new 4 KB region may page-fault (disk read); subsequent bytes in that page
> are free. So your on-disk layout should pack data that's *read together* into the *same
> page*. That's not an accident of good style — it's you controlling where the page faults
> land.

---

## 4. The cache line (64 bytes) and false sharing — you already met this

Look at your own `ringbuffer.hpp`:

```cpp
alignas(64) std::atomic<size_t> write_index{0};
alignas(64) std::atomic<size_t> read_index{0};
alignas(64) T slots[SIZE];
```

You wrote `alignas(64)` — that's the **cache line size**. You did it (correctly) to prevent
**false sharing**: if `write_index` (written by the producer) and `read_index` (written by
the consumer) shared one 64-byte line, then every write by one core would invalidate the
line in the other core's cache, forcing a coherence round-trip on the shared bus — even
though the two variables are logically independent. Padding them onto separate lines makes
each core own its line.

**This is the memory hierarchy reaching up and touching your concurrency code.** The same
64-byte granularity that makes sequential disk scans fast makes false sharing slow. One
number, two consequences. Internalize 64 bytes (cache line) and 4 KB (page); you'll see
them everywhere for the rest of the series.

> **Deeper:** cache coherence (MESI protocol) keeps cores' views consistent by passing
> line ownership around. Atomics + memory ordering (your `acquire`/`release`) are the
> *software* contract; MESI is the *hardware* mechanism that implements it. Your
> `startup-04` happens-before discussion and this are two ends of the same wire.

---

## 5. The bandwidth vs latency vs CPU trade — why compression *speeds up* reads

Beginners assume compression is a space optimization that costs time. In search engines it
is frequently a **time** optimization. Here's the reasoning, and it's pure memory
hierarchy:

- The bottleneck for scanning a big posting list is **bytes moved through the hierarchy**
  (disk→RAM→cache bandwidth), not CPU cycles. Modern CPUs are *starved* for data — they can
  decode far faster than RAM/SSD can feed them.
- If you compress a posting list 4×, you move **4× fewer bytes / touch 4× fewer pages /
  fill 4× fewer cache lines**. The CPU spends a few extra cycles decoding, but those cycles
  were going to be spent *waiting for memory anyway*.
- Net: **compressed-and-decoded is often faster than raw**, because you traded cheap,
  abundant CPU for scarce, expensive memory bandwidth.

This is *the* reason posting lists use delta + block encoding (doc 04), why columnar
formats compress each column, and why frontier engines pick codecs (FOR/PFOR/varint) tuned
for **decode speed**, not maximum compression ratio. The goal isn't "smallest file"; it's
"fewest slow bytes moved, decodable at streaming speed."

> **Systems mantra #2:** *CPU is cheap and getting cheaper; memory bandwidth and I/O are
> expensive and barely improving.* Spend the cheap thing to save the expensive thing.

---

## 6. Random vs sequential, quantified — and why B+Trees and LSM look the way they do

Put numbers on §2 to see why on-disk structures are shaped as they are:

- 1 million random 8-byte reads from SSD @ ~50 µs each ≈ **50 seconds**.
- 1 million sequential 8-byte reads that ride ~2000 pages (4 KB each) of streamed data ≈
  a few **milliseconds** (bandwidth-bound, prefetched).

Four orders of magnitude. So on-disk structures are engineered to **convert random access
into sequential access** and to **minimize the number of pages touched**:

- **B+Tree** — high **fan-out** (hundreds of keys per node) so the tree is *shallow*; each
  node = one page; a lookup touches only tree-height ≈ 3–4 pages instead of `log2(n)`
  random hops. Leaves are **linked** so range scans are sequential. Every design choice is
  "touch fewer pages, scan sequentially." (Contrast your current in-memory unbalanced
  **BST**: one node per pointer-chase, `log n` (or worse, `n`) random memory hops — fine in
  RAM at small scale, a disaster on disk.)
- **LSM-tree** — buffers writes in RAM, flushes **sorted runs** sequentially, merges them in
  the background. Turns random writes into sequential writes. (This is *exactly* the Lucene
  segment model — doc 03 — which is why immutable-append + merge keeps reappearing.)

> **Your BST → B+Tree jump reframed:** it's not "a better tree." It's *re-shaping the tree
> so its access pattern matches the memory hierarchy* — wide + shallow + page-sized nodes +
> linked leaves, so disk sees sequential block reads instead of random pointer chases.

---

## 7. The working set, and "keep the hot data hot"

The **working set** is the subset of your data actively being used over a window of time.
The whole hierarchy is a bet that the working set is *small* and *reused* (locality). Design
implications you'll use repeatedly:

- **Hot vs cold separation.** Frequently-accessed structures (the term index / FST, skip
  data, the top of a B+Tree) should be small enough to stay resident in RAM / page cache;
  bulky cold data (full posting lists, stored documents) can live on disk and be paged in on
  demand. Frontier engines *physically separate* these into different files (doc 02) so the
  OS page cache naturally keeps the hot files resident.
- **This is why the term index is an FST (compact, RAM-resident) but the postings are on
  disk.** Two structures, deliberately split by temperature.
- **This is why `Stock Status` / `Price` for filtering want columnar doc values** — a
  filter scan touches *one column* densely (great locality) instead of reading whole
  records (dragging in fields you don't need, polluting the cache).

> **Your inventory hook:** "show in-stock bakery items under R20" scans the `stock`,
> `category`, and `price` columns. If those are stored row-wise (whole product record per
> read), you drag 4 KB of reviews and descriptions through the cache to check one number.
> Columnar keeps the scan dense. That's a memory-hierarchy decision with a direct latency
> payoff on *your* queries.

---

## 8. Measuring, not guessing (bridge to your existing docs)

You have `complexity-and-measurement.md` and `performance*.md` — this doc is *why* they
matter. Big-O counts operations; the memory hierarchy means **not all operations cost the
same**. An O(n) sequential scan can crush an O(log n) structure that pointer-chases
randomly, because the constant factor is "cache hit vs disk seek" — a 10,000× spread that
Big-O hides.

- Reason about **pages touched** and **cache misses**, not just operation counts.
- When you profile your engine, watch for the tell-tale signs: cache-miss rate, page-fault
  count, resident-set size vs page-cache size. (On Windows: Resource Monitor / VMMap /
  ETW; the *concepts* transfer even if the tool differs from Linux `perf`.)

> **Systems mantra #3:** *Big-O tells you how it scales; the memory hierarchy tells you how
> much each step costs.* You need both. A cache-friendly O(n) frequently beats a
> cache-hostile O(log n) at real sizes.

---

## 9. The through-line to the rest of the series

Hold this table in your head; every later doc is an instance of it:

| Later mechanism (doc) | The memory-hierarchy move it's making |
|---|---|
| Immutable segments (03) | Sequential append + merge; no random in-place writes; page-cache-friendly |
| mmap + OS page cache (02) | Let the kernel keep the hot working set in RAM; page faults for cold |
| Posting **delta + block** encoding (04) | Fewer bytes/pages moved; decode faster than memory feeds |
| **Skip lists** in postings (04) | Skip *pages you don't need to read* during intersection |
| **BlockMax WAND** (06) | Skip *scoring whole blocks* that can't reach the top-K |
| Columnar **doc values** (02/06) | Dense, single-column scans for sort/filter — cache-friendly |
| **Merge** policy (07) | Keep segment count low → fewer files/pages to touch per query |

Every single one is "touch fewer/closer blocks, or move fewer bytes." That's the game.

---

## 10. Before you move on

Answer in your own words:

1. Why is "how many blocks do I touch, and are they contiguous?" a better question than
   "how many bytes do I need?"
2. Why can compression make a scan *faster*, not slower? Which resource are you spending and
   which are you saving?
3. What is a page fault, and why does it make `mmap` access "a disk read disguised as a
   memory read"?
4. Why does your `alignas(64)` in the ring buffer exist, and how is that the *same* number
   that makes sequential disk scans fast?
5. Reframe "BST → B+Tree" purely in memory-hierarchy terms — no hand-waving about "balanced
   is better."
6. Give one query on *your* inventory data where columnar storage beats row storage, and say
   why in terms of cache lines / pages.

Next: **02 — Storage Layout**, where we turn "pages and blocks" into concrete file formats:
page IDs, offsets, metadata, and the mmap-vs-buffer-pool decision you already got a preview
of.
