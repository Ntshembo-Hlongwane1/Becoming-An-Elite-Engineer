# Performance at Scale: What 100,000 Documents Break

## Who This Is For

You hit a wall the moment you grew the corpus. On 17 seed files everything was instant. On 100,000 files, indexing takes **minutes**, a naive parallel rewrite **crashes**, common-word queries dump tens of thousands of results, and ranking is impossible because the index doesn't even store the numbers ranking needs. Nothing is *broken* in the sense of a bug — the code that was *correct and appropriate* at 17 files is *the wrong design* at 100,000. That is the lesson scale teaches, and you can only learn it once the data is big enough to make the failures real.

This document is the **hub of the performance series**, exactly as `parsing.md` is the hub of the parsing series. It does three things:

1. Gives you the **mental model** — the *Four Walls* every system hits as data grows — so the dozen separate-looking problems collapse into four predictable categories.
2. Maps **every concrete symptom you'll observe at 100k** to the wall it belongs to and the concept that fixes it, pointing you at the right deep-dive doc.
3. Hands you the **exercises** — done in order, they rebuild the engine into one that survives 100k, and by the end you will be able to address every issue on your own.

> **Read `complexity-and-measurement.md` first.** It is the `ebnf-notation.md` of this series: the lens for *reading cost*. Every claim below ("this is `O(n²)`", "this allocates per token", "this won't overflow") is written in that language. Without it, this doc is a list of assertions; with it, it's a set of conclusions you can verify.

The two deep-dive docs split the engine the way **real search engines split themselves** — into a part that *builds and stores* the index and a part that *queries* it:

| Doc | Side | The question it answers |
|-----|------|-------------------------|
| `performance-approach1.md` | **Read / query side** | How must the index be *shaped* so queries stay fast, small, correct, and *rankable* at 100k? |
| `performance-approach2.md` | **Write / build side** | How do you *build* that index in parallel without crashing, and *store* it so you don't rebuild it every run? |

Lucene, Elasticsearch, and every production engine draw this same line between the *indexer* and the *searcher*. You're about to draw it in your own code.

---

## The Mental Model: The Four Walls of Scale

When data grows, a program does not fail randomly. It hits one of exactly **four walls**, every time, in every system ever built. Learn the four and you can predict *which* wall any given symptom belongs to — and you stop firefighting and start engineering.

```
        ┌───────────────────────────────────────────────────────────────┐
        │                      YOUR DATA GROWS 10×                       │
        └───────────────────────────────────────────────────────────────┘
                │             │              │                │
                ▼             ▼              ▼                ▼
        ╔═══════════╗  ╔═══════════╗  ╔═════════════╗  ╔═══════════════╗
        ║  WALL 1   ║  ║  WALL 2   ║  ║   WALL 3    ║  ║    WALL 4     ║
        ║   TIME    ║  ║  MEMORY   ║  ║ CONCURRENCY ║  ║ DURABILITY/IO ║
        ╠═══════════╣  ╠═══════════╣  ╠═════════════╣  ╠═══════════════╣
        ║ work per  ║  ║ footprint ║  ║ one core    ║  ║ rebuilt every ║
        ║ query/    ║  ║ + GC      ║  ║ isn't       ║  ║ run; 100k     ║
        ║ build     ║  ║ churn     ║  ║ enough;     ║  ║ tiny files;   ║
        ║ grows too ║  ║ blows the ║  ║ sharing is  ║  ║ no saved      ║
        ║ fast      ║  ║ RAM/GC    ║  ║ unsafe      ║  ║ state         ║
        ╚═══════════╝  ╚═══════════╝  ╚═════════════╝  ╚═══════════════╝
          approach1      approach1       approach2        approach2
```

- **Wall 1 — Time.** Some operation's *work* grows faster than the data. The naive `O(n×m)` intersection and the `O(result-size)` "print every match" both live here. Cure: better algorithms and data structures (sorted postings, merge intersection, top-K heap).
- **Wall 2 — Memory.** The footprint grows past comfort, or the *allocation churn* drowns the garbage collector. Duplicate filename strings, no doc IDs, `append` reallocation storms. Cure: compact representations (integer IDs, deduped postings, presized slices) and locality.
- **Wall 3 — Concurrency.** A single CPU core can't finish in time, so you parallelize — and immediately discover that *sharing mutable state is unsafe*. The `fatal error: concurrent map writes` crash lives here. Cure: worker pools + a safe strategy for the shared index (mutex / sharding / partial-merge).
- **Wall 4 — Durability & IO.** The work survives only as long as the process; every run pays the full build cost again. Plus the filesystem itself groans under 100k tiny files. Cure: persist the index (serialize/load) and understand the IO reality.

Every symptom you'll meet is one of these four. The table below sorts them.

---

## The Symptom → Wall → Concept Map

This is the index to the whole series. Find your symptom; follow the row.

| Symptom you observe at 100k | Wall | Root cause (in *your* code) | Concept that fixes it | Deep dive |
|---|---|---|---|---|
| Indexing takes minutes | 1 + 3 | `Tokenize` runs in one goroutine, serially | Worker pool, parallel build | approach2 |
| `Found 87,214 results: [...]` floods the screen | 1 | `v2` prints the entire postings slice | Top-K with a min-heap | approach1 |
| `redis AND replication` is sluggish | 1 | naive `O(n×m)` nested-loop intersection | Sorted postings + merge `O(n+m)` | approach1 |
| Can't rank results at all | 1 | index stores no term frequencies | Postings of `(docID, freq)`, TF-IDF | approach1 |
| `Found N` count is wrong (too high) | 2 | `UpdateSearchIndex` appends on every occurrence → duplicates | Dedup / postings as sets | approach1 |
| Index eats hundreds of MB | 2 | filenames stored as repeated strings | Integer doc IDs + a doc table | approach1 |
| GC pauses, CPU "in the runtime" | 2 | millions of tiny string allocations + `append` copies | Presize, intern, reduce allocs | approach1 |
| Parallel rewrite panics instantly | 3 | concurrent writes to one `map` | Mutex / sharded map / partial-merge | approach2 |
| "too many open files" when parallelizing | 3 | a goroutine per file → FD exhaustion | Bounded worker pool | approach2 |
| 8 cores ≠ 8× faster | 3 | serial merge + IO-bound workload | Amdahl's law, fan-in, the real bottleneck | approach2 |
| Every run re-indexes from scratch | 4 | index lives only in memory | Persist & load the index | approach2 |
| 25 MB of text uses 185 MB on disk | 4 | 100k tiny files, cluster slack, syscall overhead | Segment files, batched IO | approach2 |

Notice the shape: **the read side (approach1) is mostly Walls 1 & 2** (make each query cheap and the index small), and **the write side (approach2) is mostly Walls 3 & 4** (build it fast and don't lose it). That split is why the two docs exist.

---

## Why We Teach on the Real Engine (not a toy)

The parsing series used a *calculator* as a neutral domain because boolean queries and arithmetic are the same problem in disguise. Performance is different: the concepts — complexity, locality, contention, durability — are **already general**, and the most honest place to feel them is on data that actually exhibits them. You have that now: 100,000 real documents. So the exercises run **directly against your engine**, with small worked examples for checking and the full corpus for feeling the wall. Where a tiny illustration helps, we use one; but the proving ground is your own `./data`.

This is also why the exercises are *measurement-first*. In parsing, "correct" was a yes/no you could read off a trace. In performance, "better" is a *number*, and a fix that isn't measured isn't a fix — it's a guess (see `complexity-and-measurement.md`, Part 7). So Exercise 1 builds the ruler before anything else gets rebuilt.

---

## Exercises

Do these **in order**. Each one removes one wall and sets up the next. By the end, the engine indexes 100k files in parallel, in seconds, into a compact integer-keyed index it loads from disk, and answers ranked multi-term queries without flooding the screen. That is the whole payoff — and it's also Versions 5, 6, and 10 of the project, built on a foundation that won't collapse.

Each exercise states a **Goal**, the **Why**, your **Task**, **Worked Examples / Expected Shape**, and an **Integration** step that wires it into the real engine. Do not skip the measurement steps — they are the point.

---

### Exercise 1 — Build the Ruler (Baseline Measurement)

> **The foundation. You cannot improve what you cannot measure.** Before changing any design, capture exactly how the current engine behaves at scale. Every later exercise is judged against the numbers you record here.

**Goal:** Produce a repeatable baseline for (a) full-corpus index build time and (b) query cost, using a Go benchmark and ad-hoc timing.

**Why this matters:** Optimization without a recorded baseline is rearranging code and hoping. The discipline from `complexity-and-measurement.md` Part 6–7 starts now and never stops.

#### Your Task

1. Add ad-hoc timing around the whole index build (wrap the `FileReader` start path):

```go
start := time.Now()
// ... build the index over ./data ...
fmt.Printf("indexed %d docs in %s\n", n, time.Since(start))
```

2. Write a real benchmark in `src/internal/store/store_test.go` for a single common-word query against a *populated* index:

```go
func BenchmarkQueryCommonTerm(b *testing.B) {
    s := buildIndexForTest("../../../data") // populate once
    b.ResetTimer()
    for i := 0; i < b.N; i++ {
        _ = s.GetSearchIndex()["performance"] // a high-frequency term
    }
}
```

3. Run `go test -bench=. -benchmem ./src/internal/store` and **write down** `ns/op`, `B/op`, `allocs/op`, plus the build time.

#### Expected Shape

You're not chasing a target number; you're recording *your* numbers. Expect build time in the **minutes**, and note how large the `"performance"` postings slice is (`len`) — that length is the thing Exercise 5 will stop you from printing.

**Integration:** Keep `store_test.go`. Every later exercise re-runs this and compares. Paste your baseline numbers into a comment at the top of the test file so they're never lost.

---

### Exercise 2 — Integer Document IDs (Wall 2: Memory)

> **Stop storing the same filename a million times.** Replace string postings with integer doc IDs and a single document table. This is the single highest-leverage change to the index's memory footprint.

**Goal:** Convert `searchIndex` from `map[string][]string` (term → filenames) to `map[string][]int` (term → doc IDs), backed by a `documents []string` table and a `map[string]int` name→ID lookup.

**Why this matters:** A filename string is ~32 bytes (text + header); a doc ID is 8. Identical postings become identical integers you can dedupe and sort. Same Big-O, 4×+ smaller constant, and the foundation every later exercise needs. (`performance-approach1.md` Parts 1–2.)

#### Your Task

```go
type Store struct {
    documents []string        // ID -> filename;  documents[12345] = "redis-012345.txt"
    docID     map[string]int  // filename -> ID   (assign on first sight)
    index     map[string][]int
}

// returns the stable integer ID for a filename, assigning one if new
func (s *Store) internDoc(name string) int { /* you implement */ }
```

When tokenizing, look up the doc's ID once, then append the *int* to each term's postings.

#### Worked Example

```text
documents: ["redis-000000.txt", "kafka-000001.txt", "postgres-000002.txt"]
docID:     {"redis-000000.txt":0, "kafka-000001.txt":1, "postgres-000002.txt":2}
index:     {"redis":[0], "kafka":[1], "database":[0,2], "supports":[0,1,2]}
```

To print results, map IDs back through `documents[id]`.

**Integration:** Re-run Exercise 1's benchmark with `-benchmem`. Record the drop in `B/op`. This is your first measured win — quantify it ("X× less memory").

---

### Exercise 3 — Dedup + Term Frequency (Wall 1 correctness + ranking prerequisite)

> **Fix the silent correctness bug and capture the numbers ranking needs.** Right now a doc appears in a term's postings once *per occurrence*. That inflates counts and wastes space — and yet the *count itself* is exactly what TF-IDF needs. So don't throw it away: record it.

**Goal:** Change postings from `[]int` to a list of `(docID, freq)` pairs, where each document appears **once** per term with its term-frequency count.

**Why this matters:** `Found N results` becomes correct (N = distinct docs). And ranking becomes *possible*: TF needs per-doc term counts, IDF needs `df = len(postings)` and `N = len(documents)`. The old structure literally cannot compute either. (`performance-approach1.md` Parts 3–4.)

#### Your Task

```go
type Posting struct {
    DocID int
    Freq  int   // how many times the term appears in this doc
}
// index: map[string][]Posting, postings kept sorted by DocID (Exercise 4 relies on it)
```

While tokenizing a single document, accumulate counts in a small `map[string]int` (term→count for *this doc*), then flush into the global index — so each doc contributes exactly one `Posting` per term.

#### Worked Example

```text
redis-000000.txt contains "redis" 5×, "cache" 2×, "memory" 1×

index["redis"]  → [{DocID:0, Freq:5}, ...]
index["cache"]  → [{DocID:0, Freq:2}, ...]
df("redis")     = len(index["redis"])        // distinct docs, no duplicates
idf("redis")    = log(N / df("redis"))       // N = len(documents)
```

**Integration:** Verify `Found N` now equals distinct documents (compare to a manual count for a rare term). Confirm a duplicate-heavy term's postings length dropped. Record the memory delta.

---

### Exercise 4 — Sorted Postings + Merge Intersection (Wall 1: Time)

> **Kill the `O(n×m)` query.** With postings sorted by doc ID, two lists can be intersected in a single linear walk — the same merge you'd use to combine two sorted decks of cards.

**Goal:** Implement `intersect(a, b []Posting) []Posting` in `O(n + m)` using the two-pointer merge, and drive multi-term `AND` queries through it.

**Why this matters:** At 100k, naive intersection is ~625M comparisons per two-term query (`complexity-and-measurement.md` Part 2). The merge makes it ~50k. This is Version 5 done correctly. (`performance-approach1.md` Part 5.)

#### Algorithm (two pointers over sorted lists)

```text
i, j = 0, 0
while i < len(a) and j < len(b):
    if a[i].DocID == b[j].DocID:  emit; i++; j++
    elif a[i].DocID <  b[j].DocID: i++      // advance the one that's behind
    else:                          j++
```

#### Worked Example

```text
a (redis):        [0, 5, 9, 12, 40]      (DocIDs, sorted)
b (replication):  [5, 12, 13, 40, 88]
merge walk →       5, 12, 40
result:           [5, 12, 40]
```

For 3+ terms, intersect the **two shortest lists first** (smaller intermediate results = less work — the smallest list bounds the answer).

**Integration:** Benchmark `redis AND replication` before (nested loop) and after (merge). Record the speedup. Confirm identical *results*, faster *time*.

---

### Exercise 5 — Top-K Ranking with a Min-Heap (Wall 1: Time)

> **Return the best 10, not all 87,000.** You don't want to sort a 60k-result list to show a page of 10. A min-heap of size K finds the top K in `O(n log K)` and never holds more than K items.

**Goal:** Score each candidate doc with TF-IDF (Exercise 3 gave you the inputs) and use a size-K min-heap to extract the top K, highest score first.

**Why this matters:** This is Version 6 (Ranking), and it's the cure for the screen-flooding symptom. A heap beats "sort everything" because K (say 10) is tiny next to n (say 60,000): `n log K` ≪ `n log n`. (`performance-approach1.md` Part 6.)

#### The heap idea

```text
keep a min-heap of at most K (score, docID) pairs:
  for each candidate doc:
      score = sum over query terms of  tf * idf
      if heap has < K items:        push
      elif score > heap.min().score: pop the min, push this one
at the end: the heap holds the top K; pop all and reverse for descending order
```

The min-heap's *root is the weakest survivor*; any new doc only needs to beat that one number to enter. That's why you never sort the whole candidate set.

#### Worked Example

```text
Query: "redis"   K=3
scored candidates: redis-000000(8.1), caching-000123(2.4),
                   redis-000040(6.7), redis-000080(5.2), kafka-000001(0.9)
top-3 heap keeps:  {5.2, 6.7, 8.1}   (drops 2.4 and 0.9)
output (desc):     redis-000000(8.1), redis-000040(6.7), redis-000080(5.2)
```

Use Go's `container/heap` (implement the 5-method interface) — wiring it up *is* the exercise.

**Integration:** Replace `v2`'s `fmt.Printf("...%v", values)` with a ranked top-K printout. The flood is gone; results are ordered by relevance.

---

### Exercise 6 — Concurrent Indexing with a Worker Pool (Wall 3: Concurrency)

> **The Version 10 centerpiece — and the crash you must cause on purpose.** First parallelize naively and *watch it panic* with `concurrent map writes`. Understanding that failure is the lesson; the worker pool + safe merge is the fix.

**Goal:** Build the index across N worker goroutines reading files in parallel, combining their work into one index *safely*, and measure the speedup over the serial baseline.

**Why this matters:** Indexing is the minutes-long bottleneck. One core isn't enough; sharing one map across cores is unsafe. This exercise teaches both halves of real concurrency: parallelism *and* safety. (`performance-approach2.md` — the whole document.)

#### Your Task — in three deliberate stages

1. **Cause the crash.** Spawn a goroutine per file (or a few workers) all writing the *same* `map`. Run with `go run -race` (or just run it). Observe `fatal error: concurrent map writes`. **Do not skip this** — feeling the failure is why the fix makes sense.
2. **Worker pool.** Fixed N workers pulling filenames from a `jobs` channel (bounded fan-out — no FD exhaustion). A `sync.WaitGroup` waits for all to finish.
3. **Safe combine — pick one and know why:**
   - **Mutex** around the shared index (simple; lock contention is the tax).
   - **Sharded map** (N sub-maps keyed by `hash(term)%N`; less contention).
   - **Per-worker partial index, merged at the end** (no shared state during work — usually fastest). ← recommended; the generator already showed you the worker-pool skeleton.

#### Expected Shape

```text
Workers: 8
Indexed 100000 files
Time: <well under the serial baseline>
```

Speedup will be **real but less than N×** — the merge is serial and the workload is partly IO-bound. Explaining *why* (Amdahl's law, IO vs CPU bound) is part of approach2.

**Integration:** Re-run Exercise 1's build timer. Record serial vs parallel time and compute the speedup factor. Run `go test -race` on the indexer and confirm it's clean.

---

### Exercise 7 — Persist & Load the Index (Wall 4: Durability)

> **Build once, not every boot.** A real engine does not re-tokenize the corpus on every start. Serialize the finished index to disk and load it back, so startup goes from minutes to milliseconds.

**Goal:** Add `SaveIndex(path)` and `LoadIndex(path)`; on startup, load from disk if present, otherwise build (Exercise 6) and save.

**Why this matters:** The kernel currently rebuilds the entire index in `StartAll` before the first query — minutes, every run. Persistence is what makes the engine usable and is the gateway to segment files and incremental indexing. (`performance-approach2.md` Part 7.)

#### Your Task

```go
func (s *Store) SaveIndex(path string) error  // serialize documents + index
func (s *Store) LoadIndex(path string) error  // restore them
```

Start with `encoding/gob` (simplest — it serializes your structs directly). Measure file size and load time. Then, as a stretch, design a compact custom binary format and compare — *measure both*, don't assume.

#### Expected Shape

```text
first run:   no index file → build (parallel) → save → query
later runs:  index file found → load in <100ms → query immediately
```

**Integration:** Wire the load/build/save decision into the kernel's startup path. Time a cold start (build) vs a warm start (load). Record both. This closes the loop: the engine is now fast to build, small in memory, correct, ranked, *and* fast to start.

---

## Summary Checklist

Before you call the engine "scaled," confirm you can answer these from memory:

- [ ] What are the Four Walls of scale, and which wall does each of your 100k symptoms belong to?
- [ ] Why was the original `map[string][]string` *correct* at 17 files but *wrong* at 100k?
- [ ] Why must you measure a baseline before optimizing, and what three numbers does `-benchmem` give you?
- [ ] Why do integer doc IDs beat filename strings — in bytes *and* in cache behavior?
- [ ] Why does appending a posting per *occurrence* both corrupt counts and waste memory, and how does `(docID, freq)` fix both at once?
- [ ] Why is naive intersection `O(n×m)` and the sorted merge `O(n+m)`? Why intersect the shortest lists first?
- [ ] Why does a size-K min-heap beat sorting the whole candidate set for top-K?
- [ ] What exactly causes `fatal error: concurrent map writes`, and what are the three strategies to make the shared index safe?
- [ ] Why is parallel speedup less than N× (Amdahl + IO-bound)?
- [ ] Why does persisting the index matter, and what does it turn a cold start into?
- [ ] For each fix: what did you *measure* before and after, and by how much did it improve?

If you can answer all of these — and you have the recorded numbers to back them — you no longer fear scale. You can look at any data structure and predict which wall it hits, prove it with a benchmark, and rebuild it to survive. That is the foundation every later project in the roadmap stands on.

Now go: read `performance-approach1.md` for the read side, `performance-approach2.md` for the write side, and do the exercises above with a profiler open.
