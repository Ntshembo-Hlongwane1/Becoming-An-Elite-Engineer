# 05 — Query Lifecycle

> Trace a query from raw string to response: **analysis** (and why it must mirror indexing),
> **parsing** into a query tree, **rewrite** (where prefix/range expand), **weighting**,
> **per-segment execution** as set operations over the `DocIdSetIterator` abstraction
> (leapfrog conjunction, priority-queue disjunction), **top-K collection**, **segment/shard
> merge**, and the two-phase **query-then-fetch**. With best-practice iterator code and the
> intersection/union algorithms worked out. Covers your item (d).

---

## 1. The whole pipeline at a glance

```
 "in-stock bread under R20"
        │  ┌──────────────────────────────────────────────────────────────────────┐
        ▼  ▼                                                                        │
 [1 ANALYZE] tokenize/normalize the text parts  ── SAME analyzer as indexing ──────┘
        ▼
 [2 PARSE]   → query tree: BooleanQuery{ MUST term:bread,
                                         FILTER range:price<20,
                                         FILTER term:stock=in }
        ▼
 [3 REWRITE] prefix/range/wildcard → concrete terms / point-ranges (the "Trie/BKD fires")
        ▼
 [4 WEIGHT]  compute idf, boosts; build a plan that can make per-segment Scorers
        ▼
 [5 PER-SEGMENT] for each segment: Scorer = set-ops over DocIdSetIterators (postings)
        ▼
 [6 COLLECT] feed (docID, score) into a bounded top-K min-heap  (doc 06 prunes here)
        ▼
 [7 MERGE]   merge per-segment top-Ks → index top-K   (ES: merge per-shard top-Ks)
        ▼
 [8 FETCH]   read stored fields for the final K docIDs → build the response
```

Steps 5–6 are the hot loop (doc 06 dives into 6). Steps 1–4 are "planning"; 7–8 are
"assembly." Let's walk each.

---

## 2. Analysis — the symmetry rule you cannot break

The **exact same** text transformation applied at index time must be applied to the query
text. If you lowercased and stripped punctuation and removed stop-words while indexing
(your `Lexer` does all three), you **must** do the identical thing to the query — or the
query term won't byte-match the indexed term.

```
 index:  "Simple Truth Bread."  → analyze → [simple, truth, bread]     (stored in index)
 query:  "BREAD"                → analyze → [bread]                     → matches ✓
 query:  "BREAD"                → NOT analyzed → "BREAD"                → matches nothing ✗
```

- Analyzer = tokenizer + filters (lowercase, punctuation strip, stop-words, stemming). The
  term dictionary contains **post-analysis** terms; queries search that dictionary, so they
  must present post-analysis terms too.
- **Keyword fields differ:** `Brand`, `SKU`, `Category` are usually **not** analyzed — stored
  verbatim as one token — because you want exact matches ("Simple Truth", not "simple" +
  "truth"). Choosing *which* fields are analyzed text vs exact keyword is a core schema
  decision for your inventory.

> **Your engine mapping:** you already have the analyzer — it's your `Lexer`
> (`splitLine` + punctuation/space removal + `StopWords::isStopWord`). The lesson: **extract
> that logic so both the indexer and the query path call the same function.** Today it lives
> only in the lexer's worker. Duplicating it (or drifting) at query time is a classic silent
> bug. One analyzer, two callers.

---

## 3. Parse — text to a query tree

The analyzed pieces plus operators become a tree of typed query nodes:

```
 BooleanQuery
 ├── MUST     TermQuery(field=text, term="bread")        ← scoring clause
 ├── FILTER   PointRangeQuery(field=price, upper=2000)   ← non-scoring (doc 5.6)
 └── FILTER   TermQuery(field=stock, term="in")          ← non-scoring
```

Node types you'll care about for inventory:

| Node | Matches | Backed by |
|---|---|---|
| `TermQuery` | one exact term | postings (doc 04) |
| `BooleanQuery` | AND/OR/NOT of children | set-ops (§5) |
| `PhraseQuery` | terms adjacent in order | postings + positions (`.pos`) |
| `PrefixQuery` | terms starting with X | term dictionary (FST/B+Tree) |
| `PointRangeQuery` | numeric in [lo,hi] | BKD points / your attribute B+Tree |

> Parsing is where your `ebnf-notation.md` and `parsing*.md` docs pay off — a query language
> ("bread AND price<20") is a small grammar. Start with something trivial (space-separated
> terms + a couple of `field:value` filters) and grow it.

---

## 4. Rewrite — where prefix and range "fire" (the fan-out clarified, again)

**Multi-term queries rewrite themselves into primitive queries** before execution:

- `PrefixQuery("bre")` → walk the term dictionary (FST/B+Tree range scan, doc 02 §3) to
  enumerate `{bread, breakfast, bream, ...}` → rewrite to `BooleanQuery(SHOULD bread, SHOULD
  breakfast, ...)`. **This is the "Trie fires" moment — as a rewrite, not a parallel
  retriever** (confirming your earlier fan-out reframing).
- `PointRangeQuery(price < 20)` → the BKD/points structure produces a **DocIdSet** (often a
  bitset) of matching docs directly — no per-term rewrite; the range structure *is* the
  iterator.
- `WildcardQuery`, `FuzzyQuery` → compile to an **automaton** intersected with the term
  dictionary (advanced; your `overview.md` v9 fuzzy lands here).

After rewrite, the tree is entirely **primitive** nodes (TermQuery, PointRange producing
DocIdSets) combined by BooleanQuery. Uniformity is the point: execution (§5) only has to know
how to iterate and combine `DocIdSetIterator`s.

> **Guardrail:** a broad prefix ("a") can rewrite into *thousands* of terms — a real
> performance cliff. Frontier engines cap expansion or switch strategies. Note it; you'll
> want a limit.

---

## 5. Per-segment execution — the `DocIdSetIterator` abstraction

This is the unifying idea of query execution. **Every source of matches — a posting list, a
range bitset, a boolean combination — exposes the same tiny interface:**

```cpp
#include <cstdint>
#include <limits>

class DocIdSetIterator {
public:
    static constexpr std::uint32_t NO_MORE_DOCS = std::numeric_limits<std::uint32_t>::max();
    virtual ~DocIdSetIterator() = default;

    virtual std::uint32_t docID() const = 0;      // current doc, or NO_MORE_DOCS
    virtual std::uint32_t nextDoc() = 0;          // advance to next match; returns new docID
    virtual std::uint32_t advance(std::uint32_t target) = 0; // first match with docID ≥ target
    // (advance is where skip lists from doc 04 §6 earn their keep)
};
```

A `TermQuery`'s iterator decodes postings blocks; a `PointRangeQuery`'s iterates a bitset; a
`BooleanQuery`'s **composes** child iterators. Because they share the interface, AND/OR/NOT
are generic algorithms over iterators. Two you must know:

### Conjunction (AND / MUST) — leapfrog

To find docs in **all** lists, exploit sortedness: advance the list that's *behind* to catch
up to the *furthest* list; when all agree, it's a match. Always drive from the **rarest**
list (fewest postings) so you take the biggest skips.

```cpp
// Intersect k sorted iterators. `its` sorted by cost ascending (rarest first) helps skips.
std::uint32_t conjunctionNext(std::vector<DocIdSetIterator*>& its) {
    if (its.empty()) return DocIdSetIterator::NO_MORE_DOCS;
    std::uint32_t target = its[0]->nextDoc();     // lead on the rarest list
    for (;;) {
        bool allMatch = true;
        for (auto* it : its) {
            std::uint32_t d = it->docID();
            if (d < target) d = it->advance(target); // leapfrog this list up to target
            if (d == DocIdSetIterator::NO_MORE_DOCS) return DocIdSetIterator::NO_MORE_DOCS;
            if (d > target) { target = d; allMatch = false; break; } // new high-water; restart
        }
        if (allMatch) return target;              // every list is on `target` → a hit
    }
}
```

Trace `A=[1,3,5,7,100]`, `B=[3,7,100]`: lead A→1; B.advance(1)→3>1 so target=3; A.advance(3)→3,
B on 3 → match 3. Next: A→5; B.advance(5)→7>5 target=7; A.advance(7)→7 → match 7. Next: A→100;
B on 100 → match 100. Notice B **skipped** 5 and A skipped nothing it didn't need — that's the
skip list (doc 04 §6) turning intersection sublinear.

### Disjunction (OR / SHOULD) — a min-heap of iterators

To union sorted lists, always emit the **smallest current docID**; keep the iterators in a
**priority queue keyed by `docID()`**:

```
 heap top = iterator with smallest docID → that docID is the next union result;
 advance all iterators currently sitting on that docID, re-heapify, repeat.
```

This is the same bounded-heap muscle you'll use for top-K (doc 06) — a priority queue is the
workhorse of query execution.

> **Per-segment, then across segments:** the above runs **within one segment**. The searcher
> runs it on every live segment (doc 03) — often in parallel on multiple threads — each
> producing its own results, merged in §7. *This* is the real fan-out: across segments/shards,
> not across structure types.

---

## 6. Scoring vs filtering — two contexts, one big optimization

Boolean clauses come in two flavors, and the difference is a major performance lever:

- **Scoring context (`MUST`/`SHOULD`)** — contributes to the BM25 relevance score (doc 06).
  Must compute scores → can't be blindly cached (scores depend on the whole query).
- **Filter context (`FILTER`/`MUST_NOT`)** — pure yes/no membership, **no score**. Produces a
  `DocIdSet` (bitset). Because it's query-independent and segment-scoped, it's **cacheable** —
  this is the **node query cache** (doc 03 §, doc 07): the bitset for `price < 20` on segment
  `_2` is computed once and reused across queries, valid until `_2` is merged away.

For your inventory engine this is huge: `stock = in`, `category = bakery`, `price < 20`,
`rating ≥ 4` are all **filters** — non-scoring, cacheable, and combinable as fast bitset
AND/OR. Only the free-text part ("bread") needs scoring. Model attribute constraints as
filters and you get caching + speed for free.

```
 score once:   text:"bread"                (BM25)
 filter+cache: price<20 AND stock=in AND category=bakery   (bitset intersection, reusable)
 final matches = scored(bread) AND filterBitset
```

---

## 7. Merge and the two-phase query-then-fetch

**Merge:** each segment (and in ES, each shard) returns its own top-K `(docID, score)`. A
coordinator merges these small sorted lists into the global top-K with a priority queue —
tiny work, since each source already pruned to K.

**Query-then-fetch (two phases):**

1. **Query phase** returns only `(docID, score/sort-values)` — *not documents*. Cheap to
   move; enough to compute the global top-K.
2. **Fetch phase** takes the final K docIDs and reads their **stored fields** (`.stored`,
   doc 02) to build the response the user sees (title, price, image URL…).

```
 seg0 top-K ─┐
 seg1 top-K ─┼─► merge (PQ) ─► global top-K docIDs ─► FETCH stored fields ─► results
 seg2 top-K ─┘
```

Why two phases: fetching stored documents is expensive (random reads of bulky records), so
you do it **only for the final K**, not for every candidate. This is why postings hold
`docID`s (small ints) and a *separate* store holds the fat records — temperature separation
(doc 01 §7, doc 02 §1) realized as a two-phase execution.

> **Your engine mapping:** keep a `docID → filepath` table (from doc 04 §9). Query phase
> works purely in docIDs + scores; fetch phase reads the product file(s) for the final K to
> render. Don't drag product text through the scoring loop.

---

## 8. The whole trip for your example query

```
 "in-stock bread under R20"
 [1] analyze text → [bread]
 [2] parse → BOOL{ MUST text:bread ; FILTER price<2000 ; FILTER stock:in }
 [3] rewrite → primitives (no prefix here; ranges → point iterators)
 [4] weight → idf(bread), no boosts
 [5] per segment: scorer(bread postings) leapfrogged AND filterBitset(price<2000 ∧ stock:in)
 [6] collect top-K by BM25 into a size-K min-heap (doc 06 prunes with BlockMax WAND)
 [7] merge segment top-Ks → global top-K docIDs
 [8] fetch stored fields for those K → [ {Simple Truth Bread, R16.07, ...}, ... ]
```

Every bracket is a doc: [1]=§2, [3]=§4, [4]/[6]=doc 06, [5]=§5 + doc 04, [7]/[8]=§7.

---

## 9. Before you move on

1. Why must query analysis exactly mirror index analysis? Give the byte-level reason.
2. What does query *rewrite* do, and why does making everything primitive simplify
   execution?
3. Implement/trace leapfrog intersection on `A=[2,4,4,8,9]`(dedup)`, B=[1,4,9]` — which docs
   match, and which get skipped?
4. Why keep iterators in a priority queue for disjunction?
5. Filter vs scoring context: which is cacheable and why? Which of your inventory attributes
   are filters?
6. Why is query-then-fetch two phases instead of one, in memory-hierarchy terms?

Next: **06 — Ranking Pipeline**, where step 6 opens up: BM25 scoring in full, the bounded
top-K min-heap, and the early-termination algorithms — MaxScore, WAND, and BlockMax WAND —
worked through with the per-block impacts from doc 04, plus two-phase rescoring for
inventory business rules.
