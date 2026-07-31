# 06 — Ranking Pipeline

> Step 6 of the query lifecycle, opened up. **Candidate generation** → **BM25 scoring**
> (every term explained) → the bounded **top-K min-heap** → **early termination**: MaxScore,
> WAND, and **BlockMax WAND** (using the per-block impacts from doc 04) → **two-phase
> rescoring** for inventory business rules. With the BM25 formula worked numerically, a
> best-practice heap collector, and the WAND pivot loop in code. Covers your item (e).

---

## 1. The four stages, and the one question that drives them

```
 CANDIDATE GEN ─► SCORE ─► TOP-K SELECT ─► (RESCORE)
   which docs      how good   keep best K     re-rank the K with
   match?          is each?   seen so far     an expensive/business model
```

The question that shapes everything: **"the user wants the best 10 of a million matches —
how do we avoid scoring the other 999,990?"** Stages 1–3 are a machine for **not doing
work**. Stage 4 is where you *add* expensive work, but only on a tiny survivor set.

---

## 2. Candidate generation — you already built it (doc 05)

Candidates are the docs the `DocIdSetIterator` tree emits: the leapfrog conjunction / PQ
disjunction of postings, AND-ed with filter bitsets. Nothing new here — but note the framing:
candidate generation decides *the set*; scoring decides *the order*. Keeping them separate is
what lets pruning (§5) skip candidates it can prove won't matter.

---

## 3. BM25 — the scoring function, every term explained

BM25 is the default relevance score in Lucene since v6 (it replaced classic TF-IDF). For a
query term `t` in document `d`:

```
                                 f(t,d) · (k1 + 1)
 score(t,d) = IDF(t) · ─────────────────────────────────────────
                        f(t,d) + k1 · (1 − b + b · |d| / avgdl)
```

Piece by piece — *understand each, don't memorize*:

- **`f(t,d)` — term frequency.** How often `t` appears in `d`. More = more relevant… but with
  **diminishing returns**.
- **`k1` (≈1.2)** controls **tf saturation**. The `f/(f+k1·…)` shape means the 10th
  occurrence adds far less than the 2nd. (Classic TF-IDF grew linearly — a doc stuffing a word
  100× would score absurdly high. BM25 saturates.)
- **`b` (≈0.75) + `|d|/avgdl` — length normalization.** `|d|` = document length, `avgdl` =
  average across the corpus. A term matching in a *short* field ("Bread" in a 3-word
  ProductTitle) counts more than in a long one (the 400-word marketing blurb). `b` dials how
  strongly length is penalized.
- **`IDF(t)` — inverse document frequency.** `log((N − n + 0.5)/(n + 0.5) + 1)` where `N` =
  total docs, `n` = docs containing `t`. **Rare terms score higher.** "schweppes" (few docs)
  is more discriminating than "product" (every doc). This is why stop-words (near-zero IDF)
  barely matter.

Worked intuition on your data: query "rooibos". `N`=all products, `n`=few contain "rooibos" →
**high IDF**. In the Schweppes ice-tea record, "rooibos" appears in the short title and
description → decent `f`, short field → length norm favorable → **high score**. In a product
that merely mentions "rooibos" once in a 400-word blurb → same `f`=1 but long field → lower
score. BM25 ranks the ice tea first. That's the whole point.

Best-practice scorer (precompute the query-constant parts **once**, not per doc):

```cpp
struct Bm25Weight {                 // computed once per term per query
    double idf;                     // depends on N and docFreq(t)
    double k1 = 1.2, b = 0.75;
    double avgdl;                   // corpus average field length
};

// per-candidate: freq = f(t,d), docLen = |d| (both cheap to read from postings/norms)
inline double bm25(const Bm25Weight& w, double freq, double docLen) noexcept {
    const double denom = freq + w.k1 * (1.0 - w.b + w.b * docLen / w.avgdl);
    return w.idf * (freq * (w.k1 + 1.0)) / denom;
}
```

- **`idf`, `avgdl`** come from **corpus statistics** gathered at index/weight time (doc 05
  §4). `docFreq(t)` and `N` live in the term dictionary / segment metadata.
- **`docLen`** is stored per doc as a **norm** (Lucene `.nvd`, usually **quantized to 1 byte**
  to save space — precision isn't critical for ranking). You precompute and store it at index
  time; scoring just reads it.

> **A multi-term query sums per-term scores:** `score(d) = Σ_t score(t,d)`. That additive
> structure is exactly what makes the pruning in §5 possible — you can bound the total by
> bounding each term's contribution.

---

## 4. Top-K selection — a bounded min-heap

You want the K highest scores from a stream of `(docID, score)`. The tool is a **min-heap of
size K**: the smallest score sits at the root, so the root is the **current threshold** — the
score a new candidate must beat to enter.

```cpp
#include <queue>
#include <vector>
#include <cstdint>

struct ScoredDoc {
    double score;
    std::uint32_t docID;
    // min-heap: std::priority_queue is a max-heap by default, so REVERSE the comparison
    bool operator<(const ScoredDoc& o) const { return score > o.score; }
};

class TopKCollector {
public:
    explicit TopKCollector(std::size_t k) : k_(k) {}

    void collect(std::uint32_t docID, double score) {
        if (heap_.size() < k_) {
            heap_.push({score, docID});
        } else if (score > heap_.top().score) {   // beats the weakest survivor
            heap_.pop();
            heap_.push({score, docID});
        }
        // else: can't make the top-K → dropped. This is the seed of pruning (§5).
    }

    double threshold() const {                    // current K-th best; 0 until heap is full
        return heap_.size() < k_ ? 0.0 : heap_.top().score;
    }
private:
    std::size_t k_;
    std::priority_queue<ScoredDoc> heap_;          // min-heap via reversed operator<
};
```

- Space is **O(K)**, not O(matches). You never materialize a million scores.
- `threshold()` is the pivotal value: once the heap is full, **any candidate scoring ≤
  threshold is useless.** §5 is entirely about *proving* candidates are useless *before*
  scoring them.

> **This is your "TopK data structure," concretely** — an ephemeral, per-query size-K heap,
> not a maintained cache (recall the correction from our discussion). Freshness (doc 07's
> caches) is a *different* layer sitting in front of this.

---

## 5. Early termination — how to skip most candidates

Naively you score every candidate and feed the heap. Frontier engines **prune**: use
**upper-bound** scores to prove a doc (or a whole block) can't beat `threshold()`, and skip
it without full scoring. Three algorithms, increasing in power.

### 5a. Per-term max score — the upper bound you need

For each term, precompute `maxScore(t)` = the largest `score(t,d)` any doc can get for that
term (from its max tf, via §3). Then for a document matching a subset of query terms, an
**upper bound** on its total is `Σ maxScore(t)` over the terms it could match. If that bound ≤
`threshold`, the doc cannot enter the top-K. These bounds come "for free" from the impacts you
stored in doc 04 §7.

### 5b. MaxScore (Turtle & Flood)

Split query terms into **essential** and **non-essential**: sort terms by `maxScore`; the
cheapest terms whose combined `maxScore` can't lift a doc over `threshold` are "non-essential."
A doc must match at least one **essential** term to have a chance — so you only *drive
iteration* on essential terms and merely *check* non-essential ones on candidates that already
qualify. As `threshold` rises, more terms become non-essential → more skipping.

### 5c. WAND (Weak AND / Broder et al.) — the pivot trick

Keep the term iterators **sorted by current `docID`**. Walk a running sum of `maxScore` down
that sorted order until it **exceeds `threshold`**; the term where the sum crosses is the
**pivot**, and the **pivot's docID** is the smallest docID that could *possibly* make the
top-K. Advance the lagging terms straight to the pivot docID — **skipping every docID below
it.**

```cpp
// Sketch: `terms` = iterators + their maxScore, kept sorted by docID().
// Returns the next docID worth fully scoring, or NO_MORE_DOCS.
std::uint32_t wandNextCandidate(std::vector<TermScorer>& terms, double threshold) {
    for (;;) {
        sortByDocID(terms);                       // ascending current docID
        double sum = 0.0;
        std::size_t pivot = 0;
        for (; pivot < terms.size(); ++pivot) {
            sum += terms[pivot].maxScore;         // accumulate upper bounds
            if (sum > threshold) break;           // pivot found: crossing point
        }
        if (pivot == terms.size()) return DocIdSetIterator::NO_MORE_DOCS; // nothing can qualify
        std::uint32_t pivotDoc = terms[pivot].it->docID();

        if (terms[0].it->docID() == pivotDoc) {
            return pivotDoc;                       // all leading terms aligned → score it fully
        } else {
            // advance a lagging term UP to pivotDoc — skips all docIDs in between
            terms[0].it->advance(pivotDoc);        // (pick a term before pivot to advance)
        }
    }
}
```

The magic: docs whose only matching terms have a combined `maxScore ≤ threshold` are **never
scored** — WAND `advance`s right past them, and (with skip lists, doc 04 §6) past whole blocks
of postings. As the heap fills and `threshold` climbs, WAND skips ever more aggressively.

### 5d. BlockMax WAND (Ding & Suel) — the frontier default

WAND uses a *global* `maxScore(t)` — one loose bound for the whole term. **BlockMax WAND** uses
the **per-block `maxScore`** you stored in doc 04 §7 — a *tighter, local* bound. Tighter bounds
cross `threshold` later → the pivot is higher → **even more skipping**, and you can skip **whole
blocks** whose local max can't help. This is why the postings format carries per-block impacts:
the index pre-computes the bounds so the query can skip at block granularity.

```
 term "the" (huge, low idf):   blocks with maxScore 0.05, 0.04, ...  ← almost always skippable
 term "rooibos" (rare, high):  blocks with maxScore 5.2, 4.9, ...    ← the essential driver
 BlockMax WAND drives on "rooibos" blocks, skips "the" blocks that can't lift a doc over threshold
```

> **The chain, end to end:** doc 04 stores per-block max scores → doc 06 uses them as tight
> upper bounds → the top-K heap's threshold + these bounds let WAND skip blocks → the memory
> hierarchy (doc 01) rewards you because skipped blocks are never paged in or decoded. Four
> docs, one optimization.

---

## 6. Sorting instead of scoring (inventory reality)

Users often want **"sort by price, low→high"** or **"highest rating first"** — not relevance.
Then:

- You don't compute BM25; you sort by a **doc value** (columnar field, doc 02 §, doc 01 §7):
  `price`, `rating`, `stock`. The collector becomes "top-K by field value."
- **Pruning by score no longer applies** (there's no score threshold), but you can prune by
  the *sort field* if it's indexed (points/BKD give min/max per block → skip blocks outside
  the current top-K's value window — "sort optimization").
- Tie-breaking: sort by (price ASC, then score DESC, then docID) for stable, sensible order.

For your engine this matters: an inventory search box needs both **relevance mode** ("bread")
and **sort modes** ("cheapest bread"). Design the collector as an interface with a comparator
so you can swap "by score" vs "by field."

---

## 7. Two-phase ranking / rescoring — cheap wide, expensive narrow

The most important *product* idea in ranking:

```
 PHASE 1 (wide, cheap):  BM25 + WAND over ALL matches → top-N   (N = a few hundred)
 PHASE 2 (narrow, dear): re-score just those N with an EXPENSIVE model → final top-K (K=10)
```

Phase 2 can afford work that's impossible over millions: a learning-to-rank model, a
cross-encoder, or — for you — **business rules**:

- boost **in-stock** items above out-of-stock,
- boost higher **margin** or promoted **brands**,
- blend **popularity** / recent sales,
- demote items with poor **ratings** or near expiry.

This is ES's `rescore` / `function_score`. The discipline: **relevance retrieves; business
logic re-ranks a small survivor set.** Never bake business boosts into the wide phase — you'd
pay for them on every candidate and lose pruning.

> **Your inventory hook:** phase 1 = "products matching 'bread'." phase 2 = re-rank the top
> ~200 by `0.6·bm25 + 0.3·rating + 0.1·inStockBoost − expiryPenalty`. Tunable, cheap (200
> docs), and exactly how real commerce search blends relevance with merchandising.

---

## 8. The ranking pipeline for your example

```
 query "bread", K=10:
 [1] candidates: postings(bread) AND filter(price<20 ∧ in-stock)     (doc 05)
 [2] weight: idf(bread), avgdl, norms ready
 [3] iterate candidates; BlockMax WAND skips blocks whose maxScore ≤ heap.threshold()
 [4] score survivors with BM25; feed size-10 min-heap
 [5] (optional) take top-200, rescore by 0.6·bm25+0.3·rating+0.1·inStock → final 10
 [6] merge segment heaps → global top-10 docIDs → fetch stored fields (doc 05 §7)
```

---

## 9. Mapping to your engine (build order)

1. **BM25 v1:** compute `idf` from your term→docFreq counts and `N`; store per-doc field
   length (norm). Score matches, feed the `TopKCollector` above. *Now search returns ranked
   results* — a milestone your current engine can't reach (it returns unranked map hits).
2. **Sort mode:** add a comparator-based collector for "by price/rating."
3. **WAND:** add per-term `maxScore`; implement the pivot loop. Measure the skip rate.
4. **BlockMax WAND:** wire the per-block impacts from doc 04 §7 into the pivoting.
5. **Rescoring:** add a phase-2 business re-rank over the top-N.

> **Your turn:** implement step 1 and, on your real `data/`, verify a query like "rooibos" or
> "bread" ranks the on-topic product first (high IDF + short-field match). Then instrument it:
> how many candidates were scored? After adding WAND (step 3), how many were *skipped*? That
> before/after number is the whole lesson made measurable (`complexity-and-measurement.md`).

---

## 10. Before you move on

1. Explain each BM25 component (tf saturation `k1`, length norm `b`, idf) and what breaks if
   you drop it.
2. Why is the top-K structure a *min*-heap, and why is its root the "threshold"?
3. In one sentence each: MaxScore, WAND, BlockMax WAND — what does each use to prune?
4. Why does BlockMax WAND need the per-block impacts from doc 04, and why are they computed at
   index time?
5. When the user sorts by price, what changes in scoring, pruning, and the collector?
6. Why must business boosts live in phase-2 rescoring, not phase-1 retrieval?

Next: **07 — Background Workers & Scheduling**, the threads that keep this machine healthy —
merge, refresh, flush — the merge policy and write-amplification trade, cache eviction, and
how your existing `Kernal`/`Subsystem`/`RingBuffer` is already the right substrate for all of
it.
