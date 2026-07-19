# Approach 1 — The Index, Rebuilt for the Read Side (from absolute zero)

> **What this document is.** A complete, self-contained explanation of how to *shape* an inverted index so that queries stay fast, the footprint stays small, results stay correct, and ranking becomes possible — at 100,000 documents and beyond. It assumes you can program and nothing else: no information-retrieval background, no algorithms course. By the end you will understand *why* every production search engine (Lucene, Elasticsearch, Tantivy) represents its index the way it does, be able to build that representation yourself, and reason precisely about its speed and memory.
>
> This is the **read / query side**: everything here is about making *lookups* cheap. Its sibling, `performance-approach2.md`, is the **write / build side** — building this index in parallel and persisting it. Real engines split the *indexer* from the *searcher* for exactly this reason, and so will you.
>
> **Prerequisite:** `complexity-and-measurement.md`. Every cost claim below — `O(n+m)`, "32 bytes per posting", "cache miss" — is written in that document's language.

---

## Part 0 — The problem, stated plainly

Your index is one line:

```go
searchIndex map[string][]string   // term -> list of filenames
```

At 17 documents this is *perfect* — simple, correct, fast. At 100,000 documents it is **wrong in four independent ways at once**, and the reason it's wrong is that this one field is secretly trying to be *three different things* and is bad at all three:

1. It is the **dictionary** (term → where to look) — this part is fine.
2. It is the **postings list** (which documents) — but as repeated *strings*, which wastes memory and can't be sorted/deduped cheaply.
3. It is supposed to support **ranking** — but it stores no counts, so it *can't*.

And a fourth problem rides on top: because postings are appended *per occurrence*, the same document appears many times in one term's list — corrupting result counts and bloating everything.

Watch all four fail on a single query, `redis AND replication`, at 100k:

```
map[string][]string at 100k:
   index["redis"]        = ["redis-000000.txt","redis-000000.txt", ... 12,500 strings, ~80% dupes]
   index["replication"]  = ["replication-..." ... 12,500 strings]

   • intersect them naively      → O(n×m) ≈ 625,000,000 comparisons   (Wall 1: time)
   • each string ~32 bytes, repeated → tens of MB of duplication       (Wall 2: memory)
   • "Found 12,500 results"      → WRONG, only ~2,500 distinct docs    (correctness)
   • rank by relevance           → IMPOSSIBLE, no term counts stored    (can't even start)
```

This document rebuilds the index into a shape where all four problems vanish. We build it up one transformation at a time, measuring at each step.

---

## Part 1 — Document IDs: replace strings with integers

### The idea

A filename like `"redis-012345.txt"` is a 16-character string. In Go a `string` value is a **header** — a pointer to the text plus a length, 16 bytes on a 64-bit machine — *plus* the 16 bytes of text it points to. Call it ~32 bytes, and the text lives somewhere else on the heap (a pointer to chase).

The insight: **the postings list doesn't need the filename. It needs to know *which* document.** "Which" is answerable with a number. So assign every document a stable integer **document ID** the first time you see it, keep one table mapping ID → filename, and let every posting be just an `int`.

```go
type Store struct {
    documents []string        // ID is the index:  documents[12345] = "redis-012345.txt"
    docID     map[string]int  // reverse lookup:    docID["redis-012345.txt"] = 12345
    index     map[string][]int
}

// internDoc returns the stable ID for a filename, assigning the next one if new.
func (s *Store) internDoc(name string) int {
    if id, ok := s.docID[name]; ok {
        return id
    }
    id := len(s.documents)
    s.documents = append(s.documents, name)
    s.docID[name] = id
    return id
}
```

### Why this is a big win, in numbers

A posting was ~32 bytes (string header + text, scattered). A posting is now an `int` — **8 bytes**, stored *inline* in a contiguous slice. Count it (`complexity-and-measurement.md` Part 4 taught you to):

```
Postings total P ≈ tens of millions at 100k.

before:  P × ~32 bytes of strings, scattered across the heap
after:   P × 8 bytes of ints, packed in contiguous slices
         → ~4× smaller, and (Part 7) far friendlier to the CPU cache
```

The filename text is now stored **exactly once** in `documents`, instead of once per posting. That alone removes most of the index's memory. Same Big-O growth class (`O(P)`), dramatically smaller constant — the kind of win that only shows up when you *count bytes*, never in the asymptotic notation.

> **This is universal.** Every serious search engine assigns integer doc IDs (Lucene calls them "docids") for exactly these reasons: integers are small, comparable, sortable, and cache-friendly. You are not inventing a hack; you are arriving at the standard design by feeling the pain that motivated it.

---

## Part 2 — Postings lists: what they really are

The list of documents for a term is called a **postings list** (the term is from IR; "posting" = one entry recording that a term occurs in a document). It is *the* core data structure of search — the thing that makes search an indexing problem instead of a scanning problem.

Right now your postings are `[]int`. Two properties you will *add* and then *depend on*:

1. **Sorted by doc ID.** A sorted list can be intersected, unioned, and deduped in a single linear pass (Part 5). An unsorted list cannot — it forces the `O(n×m)` nested loop. Sorting is the small investment that unlocks every fast set operation.
2. **One entry per document** (no duplicates). A postings list is conceptually a *set* of documents; a document is either in it or not.

Building sorted postings is almost free if doc IDs are assigned in the order you read files (which `internDoc` does): if you index documents 0,1,2,… in order, and within each document append to each term's list, every term's postings come out **naturally ascending** — no sort needed. (When you parallelize in approach2, order is no longer guaranteed and you sort each list once at merge time. Still `O(P log(avg list))`, paid once.)

---

## Part 3 — Sets and dedup: the silent correctness bug

### The bug

```go
func (s *Store) UpdateSearchIndex(key, value string) {
    s.searchIndex[key] = append(s.searchIndex[key], value)  // appends EVERY time
}
```

`Tokenize` calls this once per *occurrence* of a word. `redis` appears 5 times in `redis-000000.txt`, so `index["redis"]` gets `0` appended 5 times. The postings list is a *multiset* (duplicates allowed) when it must be a *set*.

Consequences at scale:

- **Wrong counts.** `Found 12,500 results` when there are ~2,500 documents. The number is meaningless.
- **Wasted memory.** ~80% of postings entries are duplicates (Part 0).
- **Slower everything.** Every intersection and union processes the inflated lists.

### The fix — and why it's the same fix as ranking

You could dedupe blindly (e.g. `map[int]bool` per term, then collect keys). But notice: **the duplicate count is information you actually want.** `redis` appearing 5× in a document is a signal that the document is *more about* redis than one appearing 1×. That signal is **term frequency (TF)** — the raw material of ranking. So don't discard the duplicates; *count* them. One change fixes correctness, memory, *and* unlocks Part 4.

---

## Part 4 — The structure ranking needs: `(docID, freq)` postings

### Why the old structure literally cannot rank

Ranking by relevance (Version 6) uses **TF-IDF**, which needs three numbers:

- **TF** — term frequency: how many times the term appears *in this document*. Higher TF → more relevant.
- **DF** — document frequency: how many documents the term appears in *at all*. A term in every document (like `supports`) is uninformative; a rare term is highly informative.
- **N** — the total number of documents.

`map[string][]string` stores filenames and nothing else. **TF is gone** (you only know a doc is present, not how often the term occurs), and DF is corrupted by duplicates. You cannot rank with this structure at *any* scale — at 100k it's just *obvious* because thousands of docs tie for every query.

### The representation

A posting becomes a small struct carrying the count:

```go
type Posting struct {
    DocID int
    Freq  int   // term frequency: occurrences of this term in this document
}

// index: map[string][]Posting, each list sorted by DocID, one Posting per doc
```

Now the three numbers fall out for free:

```go
N   := len(s.documents)              // total documents
df  := len(s.index[term])            // document frequency = postings length
// for a given Posting p of this term in some doc:
tf  := p.Freq
idf := math.Log(float64(N) / float64(df))
score := float64(tf) * idf           // the TF-IDF contribution of this term to this doc
```

### Building it: count per document, then flush

The clean way to guarantee one `Posting` per doc with the right `Freq`: while tokenizing a *single* document, tally counts in a tiny local map, then flush once.

```go
func (s *Store) IndexDocument(name string, words []string) {
    id := s.internDoc(name)
    local := make(map[string]int) // term -> count, for THIS doc only
    for _, w := range words {
        local[w]++
    }
    for term, freq := range local {
        s.index[term] = append(s.index[term], Posting{DocID: id, Freq: freq})
    }
}
```

Each document now contributes **exactly one** posting per distinct term, carrying its frequency. Correctness fixed, memory reclaimed, ranking enabled — one design, three walls down.

> **TF-IDF refinements exist** (log-scaling TF, length normalization, BM25 — the modern default). Don't reach for them yet. The *structure* — `(docID, freq)` postings plus `N` and `df` — is what every scoring formula needs; the formula is a swappable detail layered on top. Get the structure right first.

---

## Part 5 — Intersection at scale: from `O(n×m)` to `O(n+m)`

Multi-term `AND` (Version 5) intersects postings lists. This is where the read side either survives or dies at scale.

### The naive way and why it dies

```go
for _, x := range a {        // n
    for _, y := range b {    // m
        if x.DocID == y.DocID { out = append(out, x) }
    }
}
```

`O(n×m)`. At 100k, two common terms (~25,000 postings each) → **625 million** comparisons per query (`complexity-and-measurement.md` Part 2). That is the `O(n²)` row. Unusable.

### The merge: walk two sorted lists with two pointers

Because postings are **sorted by DocID** (Part 2), you intersect them the way you'd merge two sorted hands of cards: one pointer in each list, always advance the pointer that's *behind*.

```go
func intersect(a, b []Posting) []Posting {
    var out []Posting
    i, j := 0, 0
    for i < len(a) && j < len(b) {
        switch {
        case a[i].DocID == b[j].DocID:
            out = append(out, a[i])   // (or combine freqs for scoring)
            i++; j++
        case a[i].DocID < b[j].DocID:
            i++                        // a is behind; catch it up
        default:
            j++                        // b is behind; catch it up
        }
    }
    return out
}
```

Each pointer only moves forward, and at least one advances every iteration, so the loop runs at most `n + m` times: **`O(n + m)`**. At 100k that's ~50,000 steps instead of 625,000,000 — a **~12,000× reduction in work**, from sorting alone.

### Trace

```
a (redis):        [0, 5, 9, 12, 40]
b (replication):  [5, 12, 13, 40, 88]

i=0 j=0:  0 < 5            → i++
i=1 j=0:  5 == 5  emit 5   → i++ j++
i=2 j=1:  9 < 12           → i++
i=3 j=1:  12 == 12 emit 12 → i++ j++
i=4 j=2:  40 vs 13: 40>13  → j++
i=4 j=3:  40 == 40 emit 40 → i++ j++   (i now off the end → stop)
result: [5, 12, 40]   ✓
```

### Two refinements that matter at scale

- **Intersect the shortest lists first.** For `a AND b AND c`, the result can be no larger than the smallest list. Intersect the two *shortest* first so intermediate results stay tiny. Sorting the lists by length before intersecting is a cheap, large win on skewed queries (one rare term + one common term).
- **Galloping (skip) search.** When one list is *much* shorter than the other (rare term AND common term), don't step through the big list one-by-one — *binary-search* (gallop) ahead to the next needed DocID. This turns `O(n+m)` into roughly `O(n log m)` where `n ≪ m`, which is faster when the lists are very lopsided. This is exactly what Lucene does. Implement the plain merge first; reach for galloping only when a benchmark says the lopsided case is hot.

> **`OR` (union) and `NOT` (difference)** are the same two-pointer merge with different emit rules: union emits from whichever pointer is behind (and both on a tie); difference emits from `a` only when `b` has no match. Sorted lists make *all* set operations linear. That is the whole reason to keep them sorted.

---

## Part 6 — Top-K: return the best 10, not all 87,000

### The problem

After scoring, a common-term query has tens of thousands of candidate documents, each with a TF-IDF score. The user wants the **top 10**. The naive approach — score everything, sort the whole list, take the first 10 — is `O(n log n)` and materializes a 60,000-element sorted slice to show 10 rows. Wasteful in both time and memory.

### The min-heap of size K

A **heap** is a tree-shaped structure that keeps its smallest element (a min-heap) instantly accessible at the root, with `O(log size)` insert and remove. The trick for top-K:

> Keep a min-heap holding **at most K** elements. The root is the *weakest survivor*. To consider a new candidate, compare it to the root: if the new score beats the weakest, evict the root and insert the newcomer; otherwise discard the newcomer. After one pass, the heap holds exactly the top K.

```text
keep min-heap H, capacity K:
  for each candidate (score, docID):
      if len(H) < K:                 push(score, docID)
      else if score > H.min().score: pop min; push(score, docID)
  // H now holds the top K; pop all and reverse → descending order
```

### Why it beats sorting

- You never hold more than K items → **O(K) memory**, not O(n).
- Each of the n candidates costs at most one `O(log K)` heap operation → **O(n log K)** time.

With n = 60,000 and K = 10: `n log K ≈ 60,000 × 3.3 ≈ 200,000` operations, versus sorting's `n log n ≈ 60,000 × 16 ≈ 960,000`. ~5× fewer, and a tiny fixed memory footprint. The gap widens as n grows and K stays small — which is the real-world case (millions of matches, one page of results). This is Version 6's data structure, and the cure for the screen-flood symptom.

### Trace

```
Query "redis", K=3. Scored candidates arrive:
  redis-000000(8.1) → H<3, push        H={8.1}
  caching-000123(2.4) → H<3, push      H={2.4, 8.1}
  redis-000040(6.7) → H<3, push        H={2.4, 6.7, 8.1}   root(min)=2.4
  redis-000080(5.2) → 5.2 > 2.4 → evict 2.4, push  H={5.2, 6.7, 8.1}  root=5.2
  kafka-000001(0.9) → 0.9 > 5.2? no → discard       H={5.2, 6.7, 8.1}
pop all + reverse → 8.1, 6.7, 5.2  →  redis-000000, redis-000040, redis-000080
```

Use Go's `container/heap`: implement the five-method `heap.Interface` (`Len, Less, Swap, Push, Pop`) on a slice of `(score, docID)`. Wiring that interface up correctly *is* the exercise — `Less` defines a *min*-heap (smallest at root) so the weakest survivor is what you compare against.

---

## Part 7 — Memory and the machine: why layout beats Big-O

You've cut the index's *growth-class* constants. Now the subtler win that `complexity-and-measurement.md` Part 5 promised: **data layout**, which Big-O cannot see but the CPU feels acutely.

### Contiguous integers vs scattered strings

```
[]int postings:      [0][5][9][12][40]      one contiguous block of 8-byte ints
                      ▲ CPU loads a whole cache line (~64 bytes = 8 ints) at once;
                        the next elements are already in cache → ~1–10 cycles each

[]string postings:   [hdr][hdr][hdr]...     headers are contiguous, BUT each header
                      │     │                points to text ELSEWHERE on the heap;
                      ▼     ▼                comparing/reading text chases a pointer
                     "redis-0..." "redis-0..."  → likely cache miss → ~100 cycles each
```

Both are `O(n)` to scan. The `[]int` is routinely **~10× faster** in wall-clock time because it streams through cache while the `[]string` thrashes it. *This is why the merge intersection on integer postings is fast in practice and not just in theory.* Locality is a first-class performance property; integers give it to you for free.

### Allocation and GC churn

The original `Tokenize` allocates aggressively: `strings.Fields` builds a slice and a fresh string per word; `removePunctuation` builds another; every `append` past capacity reallocates and copies (`complexity-and-measurement.md` Part 3). Across 100k documents that's *tens of millions* of short-lived allocations the GC must scan. Symptoms: CPU "in the runtime", periodic pauses. Mitigations, in order of leverage:

- **Presize postings:** `make([]Posting, 0, expectedDF)` avoids the doubling-copy storm when you know roughly how big a list gets.
- **Intern terms:** store each distinct term string once (a `map[string]string` or a term-ID scheme mirroring doc IDs) so you're not re-allocating `"performance"` thousands of times.
- **Reuse buffers** in the tokenizer instead of allocating per word.

Don't apply these blindly — **profile first** (`pprof` heap), fix the top allocator, re-measure. The discipline is the point; the specific fix is whatever the profile names.

---

## Part 8 — Pitfalls (the bugs you *will* hit)

**Forgetting to keep postings sorted.** The merge intersection is *only* correct on sorted lists. If a parallel build (approach2) appends out of order, you must sort each list once before serving queries. A merge over unsorted lists silently returns wrong results — no crash, just wrong answers. Always assert sortedness in a test.

**Doc-ID instability across runs.** If IDs are assigned in directory-read order and the directory changes, yesterday's persisted postings point at the wrong documents. Either persist the `documents` table *with* the index (approach2 Part 7) or assign IDs deterministically. Never mix postings from one run with a doc table from another.

**Deduping by throwing away frequency.** The tempting `map[int]bool` dedup discards TF and quietly kills ranking. Count, don't just dedupe (Part 4).

**Min-heap with the comparison backwards.** A *max*-heap at the root keeps the *strongest* item on top — so you'd evict your best results and keep the worst. For top-K you want a **min**-heap (weakest survivor at root). Test with K=3 on five known scores; if you get the bottom 3, your `Less` is inverted.

**Off-by-one in the merge.** The loop condition is `i < len(a) && j < len(b)`; the moment either runs out, the intersection is done (anything left in the other list can't match). Forgetting that emits garbage. The trace in Part 5 is your test oracle.

---

## Part 9 — When this matters, and where you've seen it

This *is* how real search engines store their indexes. Lucene (and thus Elasticsearch, Solr, OpenSearch) stores integer doc IDs in sorted postings, with term frequencies and positions alongside, and intersects them with skip-list/galloping merges; it returns top-K via heaps; it obsesses over compact, cache-friendly, on-disk-friendly layouts. Tantivy (Rust), Bleve (Go), and every database's full-text index do the same. You are not building a toy version of something exotic — you are building the actual standard design, motivated by the exact pains you measured.

The transferable lesson beyond search: **the representation of your data is the dominant performance decision.** Integer keys over string keys, sorted over unsorted, counts captured rather than recomputed, contiguous over scattered — these choices recur in databases, compilers, graphics, networking. Once you've felt why they matter here, you'll recognize the same fork everywhere.

---

## Part 10 — Self-implementation blueprint (do this from a blank file)

Build the read-side index in this order; each step is independently testable and measurable.

1. **Add the doc table.** `documents []string`, `docID map[string]int`, and `internDoc`. Test that the same filename always returns the same ID and that `documents[id]` round-trips.
2. **Switch postings to `[]int`.** Index a handful of files; print a term's postings; confirm they're ascending. **Benchmark `-benchmem`; record the memory drop** vs the string version.
3. **Add frequency: `[]Posting`.** Tally per-document counts, flush one posting per doc. Verify `Found N` equals distinct docs and `Freq` matches a hand count.
4. **Write `intersect` (merge).** Test on the Part 5 trace. **Benchmark vs the nested-loop version; record the speedup.**
5. **Add `union` and `difference`** as merge variants (for `OR`/`NOT`). Reuse the two-pointer skeleton.
6. **Add TF-IDF scoring.** Compute `idf` from `N` and `df`, `score` from `tf*idf`. Sanity-check that a doc with a rare query term outranks one with only common terms.
7. **Add top-K with `container/heap`.** Test K=3 on known scores (Part 6 trace). Confirm descending order and that you never hold more than K items.
8. **Profile and tighten constants.** `pprof` the build; presize/intern/reuse where the profile points; re-measure. Stop when the profile is flat, not when you're bored.

When all eight pass and you have the before/after numbers written down, the read side scales. Then go to `performance-approach2.md` to build this index in parallel and persist it.

---

## Part 11 — Self-check (answer from memory)

- Why is `map[string][]string` secretly three structures, and which job does it do acceptably versus badly?
- What is a document ID, why does it save memory *and* time, and how do you assign stable ones?
- What is a postings list, and what two properties (sorted, set) do all the fast operations depend on?
- Why does appending a posting per *occurrence* corrupt both counts and memory — and why is the fix the *same* fix that enables ranking?
- What three numbers does TF-IDF need, and exactly why can the original index compute none of them?
- Walk the two-pointer merge on `[0,5,9,12,40] ∩ [5,12,13,40,88]`. Why is it `O(n+m)` and not `O(n×m)`?
- Why intersect the shortest lists first? When would you reach for galloping search?
- Why does a size-K min-heap beat sorting for top-K, in both time and memory? Why a *min*-heap and not a max-heap?
- Two `O(n)` scans — `[]int` vs `[]string` — can differ 10× in speed. Why? Name the hardware reason.
- Where does GC churn come from in the tokenizer, and what three mitigations exist? What must you do *before* applying them?

If those come without flipping back, you understand the read side at the level where you could rebuild it blind — and you'll recognize the same representation choices in every index, database, and compiler you ever open. Now build the write side.
