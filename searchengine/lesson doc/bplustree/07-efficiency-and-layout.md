# 07 — Efficiency & Memory Layout

> **You have a correct B+Tree. Now make it fast.** Everything here is a *measured* change, not
> a folklore one — the rule from `complexity-and-measurement.md` applies: **benchmark before,
> benchmark after, keep the change only if the number moved.** Several of the optimisations
> below will *not* help on your machine and your data. That's a finding, not a failure.
>
> The single largest win is §3 (node layout). Do it first, measure it, and let the number
> decide whether §4–§7 are worth your time.

---

## 1. Choosing the capacities

The constants are the highest-leverage tuning knob and cost nothing to change.

### In-memory: size the node to *cache lines*, not pages

For a purely in-memory tree the transfer unit is the **64-byte cache line**, not the 4 KB
page. Naïvely that suggests fanout ≈ 3 (3 × 16-byte keys ≈ one line). **That's wrong**, for
two reasons:

1. **Hardware prefetching.** Modern CPUs detect sequential access and prefetch the *next*
   line automatically. Scanning 8 contiguous cache lines is nowhere near 8× the cost of
   scanning 1 — after the first miss, the rest arrive ahead of demand. Sequential access
   inside a node is nearly free; the *random* access between levels is what costs.
2. **The dependent-load chain (doc 01 §1).** Each level's address depends on the previous
   level's data, so misses **cannot overlap**. Reducing the number of levels reduces the
   number of *serialised* misses, which is the metric that matters.

So: **make nodes several cache lines, not one.** The empirical sweet spot for in-memory
B+Trees is **256–512 bytes per node**, giving fanout ~16–64 for small keys. Below ~16, height
grows and you pay serialised misses; above ~128, in-node scan cost starts to dominate.

```
  N = 1,000,000 keys.  Cost model: MISSES ≈ height (serialised) + in-node scan (prefetched)

  fanout   height   serialised misses   in-node comparisons
      4      10          10                   ~20
     16       5           5                   ~20
     64       4           4                   ~24
    256       3           3                   ~24
   1024       2           2                   ~20   ← but each node is 16 KB; poor cache reuse
```

Note the comparison count barely moves (it's always ~`log₂ N`, by information theory — you
cannot beat that). **What changes is how many of them are cache misses.** That's the whole
game.

### On-disk: size the node to the page

`node size == page size == 4096 bytes`, exactly. Doc 01 §2's arithmetic:
`INTERNAL_MAX = (PAGE_SIZE − header) / (keySize + pointerSize)`. Non-negotiable — a node
spanning two pages doubles your I/O per level.

### Just measure it

```cpp
template <int F> void benchFanout() {
    BPlusTree<uint64_t, uint64_t, std::less<uint64_t>, F, F> t;
    // ... insert 1M random, then time 1M random lookups ...
}
benchFanout<4>();  benchFanout<16>(); benchFanout<32>();
benchFanout<64>(); benchFanout<128>(); benchFanout<256>();
```

Thirty lines, one afternoon, and the answer is specific to your CPU, your key type, and your
access pattern. **Do this before any other optimisation in this doc** — it's the cheapest
large win available, and it recalibrates your intuition for everything below.

---

## 2. Measuring honestly

Four ways to get a meaningless number. All four are common.

**(a) Benchmarking with `-DBPTREE_VALIDATE` on.** The validator is `O(N)` per operation, so
your `O(log N)` insert becomes `O(N)` and every result is noise. Separate build targets.

**(b) Benchmarking with `-O0`.** The vectors, the comparator indirection, the small helper
functions — all of it is inlined away at `-O2` and none of it at `-O0`. `-O0` numbers can be
10× off, and *differentially* off, so relative comparisons are wrong too.

**(c) Sequential keys only.** `insert(i)` for `i = 0..N` hits the same rightmost leaf every
time — perfectly cached, no misses at all, occupancy pathology (doc 04 §8). It measures your
best case and nothing else. **Always benchmark random keys**, and separately benchmark
sequential as its own case, and report both.

**(d) Letting the optimiser delete your work.** If you don't use the lookup result, the whole
loop can be removed:

```cpp
uint64_t sink = 0;
for (auto k : queries) if (auto* v = t.find(k)) sink += *v;
// after the timing loop:
if (sink == 0xDEADBEEF) std::cerr << "";      // observable use — defeats DCE
```

### A minimal harness

```cpp
#include <chrono>
#include <random>
#include <vector>

template <typename F>
double timeMs(F&& f) {
    auto t0 = std::chrono::steady_clock::now();
    f();
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

void bench(std::size_t N) {
    std::mt19937_64 rng(42);                     // FIXED SEED — comparable across runs
    std::vector<uint64_t> keys(N);
    for (auto& k : keys) k = rng();

    BPlusTree<uint64_t, uint64_t> tree;
    double ins = timeMs([&]{ for (auto k : keys) tree.insert(k, k); });

    std::shuffle(keys.begin(), keys.end(), rng);  // query order != insert order
    uint64_t sink = 0;
    double fnd = timeMs([&]{ for (auto k : keys) if (auto* v = tree.find(k)) sink += *v; });

    std::printf("N=%zu  insert %.1f ms (%.0f ns/op)  find %.1f ms (%.0f ns/op)  h=%d\n",
                N, ins, ins * 1e6 / N, fnd, fnd * 1e6 / N, tree.height());
    if (sink == 1) std::fputs("", stderr);
}
```

**Run each size 5 times and take the minimum**, not the mean — the minimum is the run least
disturbed by scheduling and other processes, and it's the most reproducible statistic. Sweep
`N` across 10³, 10⁴, ..., 10⁷ and plot ns/op against `log N`: you should see a **staircase**,
with a step each time the working set outgrows a cache level. That staircase *is* the memory
hierarchy from frontier doc 01, drawn by your own code. It's worth the afternoon.

---

## 3. The big one: vectors → fixed arrays

Doc 02 §1 flagged this. Here's the payoff and the migration.

### Why the vector layout costs you double

```
  std::vector<Key> keys;      // 24 bytes IN the node: {pointer, size, capacity}
                              // the actual keys live SOMEWHERE ELSE on the heap

  Node in cache:              Keys in cache:
  ┌────────────────────┐      ┌──────────────────────┐
  │ isLeaf │ keys{ptr} │ ───▶ │ k0 k1 k2 k3 ...      │
  │ children{ptr} ...  │      └──────────────────────┘
  └────────────────────┘              ▲
       miss #1                    miss #2  ← a SECOND dependent miss, per level
```

**Two dependent misses per level instead of one.** At height 4 that's 8 serialised misses
where the design promised 4. You lose half the structural advantage — and it's invisible in
`-O0` profiling and invisible in the algorithm.

### The fixed-array node

```cpp
template <typename Key, typename Value, int LEAF_MAX, int INTERNAL_MAX>
struct alignas(64) Node {
    // ---- header: keep it small; it eats fanout -----------------------------
    uint16_t count  = 0;        // keys in use
    bool     isLeaf = false;

    // ---- +1 on the key array to hold the TRANSIENT overflow (doc 04 §2) ----
    Key keys[(LEAF_MAX > INTERNAL_MAX ? LEAF_MAX : INTERNAL_MAX) + 1];

    union {
        Node* children[INTERNAL_MAX + 2];   // internal: count+1 in use (+1 transient)
        Value values[LEAF_MAX + 1];         // leaf:     count   in use
    };
    Node* next = nullptr;
};
```

Gains:

- **One allocation, one contiguous block, one cache miss per level.**
- `alignas(64)` — the node starts on a cache-line boundary, so a 256-byte node is exactly
  4 lines rather than 5 straddled ones.
- **Trivially copyable** if `Key` and `Value` are → `memcpy` to a disk page (doc 08).
- No `capacity` field, no allocator call per split beyond the node itself.

Costs, and they're real:

- **`union` with non-trivial types needs manual lifetime management** (`placement new`,
  explicit destructor calls). With `Key = std::string` this is genuinely error-prone. Two
  outs: (a) skip the union — waste `max(sizeof(Value)*LEAF_MAX, sizeof(Node*)*INTERNAL_MAX)`
  bytes per node; (b) use separate `LeafNode`/`InternalNode` types with a common header (no
  virtuals — a `bool` tag and `static_cast`).
- **Inserting mid-array is a manual `memmove`** instead of `vector::insert`. It's 3 lines and
  it's what `vector::insert` does anyway:

```cpp
void insertKeyAt(Node* n, int pos, const Key& k) {
    std::move_backward(n->keys + pos, n->keys + n->count, n->keys + n->count + 1);
    n->keys[pos] = k;
    ++n->count;
}
```

- **Every `.size()` becomes `->count`** and every `.push_back` becomes explicit. Mechanical,
  but it touches every function you wrote in docs 04–05.

> **Migration strategy:** keep the vector version. Copy the file to `bplustree_flat.hpp`,
> migrate that, and run **the same test suite** (doc 09) against both — the suite is
> template-agnostic if you wrote it that way. Then benchmark both with §2's harness. Now you
> have a measured number for the change *and* a correct fallback if the flat version has a
> bug. This is the discipline that makes aggressive optimisation safe.
>
> **Expect roughly 1.5–2× on random lookups at N ≥ 10⁶**, and near-zero improvement at
> N ≤ 10⁴ (everything's in L2 either way). If you see no improvement at large N, your
> benchmark is measuring something else — go back to §2.

---

## 4. In-node search: linear beats binary (for a while)

Textbook says binary search: `O(log f)` vs `O(f)`. The hardware disagrees for small `f`.

| | Linear scan | Binary search |
|---|---|---|
| Comparisons | `f/2` average | `log₂ f` |
| Branch prediction | **Excellent** — one mispredict at the exit | **Terrible** — each step is ~50/50, ~15-cycle mispredict penalty |
| Memory access | **Sequential** — prefetcher-friendly | **Random within the node** — jumps around |
| Vectorisable | **Yes** — SIMD compares 8–16 keys per instruction | No |

For `f ≤ 16`, linear typically wins outright. Crossover is usually **32–64**, but it depends
heavily on `sizeof(Key)` and whether comparison is cheap (`uint64_t`) or expensive
(`std::string`, which is a `memcmp` call).

```cpp
int lowerBoundIdx(const Key* ks, int n, const Key& k) const {
    if constexpr (LEAF_MAX <= 32) {
        int i = 0;
        while (i < n && less_(ks[i], k)) ++i;      // vectorisable, predictable
        return i;
    } else {
        int lo = 0, hi = n;                        // branchless-ish binary (below)
        while (lo < hi) { int mid = (lo + hi) / 2;
                          if (less_(ks[mid], k)) lo = mid + 1; else hi = mid; }
        return lo;
    }
}
```

**Branchless binary search** removes the mispredict penalty by turning the branch into
arithmetic — the CPU executes both sides and selects (`cmov`):

```cpp
int branchlessLowerBound(const Key* ks, int n, const Key& k) const {
    int base = 0;
    while (n > 1) {
        int half = n / 2;
        base += less_(ks[base + half - 1], k) ? half : 0;   // compiles to CMOV
        n    -= half;
    }
    return base + (n > 0 && less_(ks[base], k) ? 1 : 0);
}
```

Verify with `objdump -d` that you actually got `cmov` and not a jump — compilers decide this
heuristically and will sometimes ignore you. **This is a real optimisation with a real cost
in readability. Only keep it if §2's harness shows a win** — for `std::string` keys, the
comparison itself dominates and the branch is irrelevant, so it will show nothing.

> **Do the `if constexpr` version regardless.** It's free, it's readable, and it picks the
> right algorithm per instantiation — so your `<4,4>` test tree and your `<128,128>` production
> tree each get the right one from one source.

---

## 5. Software prefetching

Since each level's address depends on the previous level's *data*, the hardware can't
prefetch across levels. But **you** know the candidate children the moment the node is
loaded — so you can issue a prefetch for the likely child before you've finished searching:

```cpp
while (!node->isLeaf) {
    __builtin_prefetch(node->children[node->count / 2]);   // guess the middle child
    int i = upperBoundIdx(node->keys, node->count, key);   // ~20-40 cycles of search
    node  = node->children[i];                             // by now the guess may have landed
}
```

The prefetch overlaps the DRAM latency of the *next* level with the in-node search of the
*current* one. It's a guess (`1/f` hit rate on the exact child) — but prefetching the middle
child often pulls in a line shared with neighbours, and a wrong guess costs only wasted
bandwidth.

**Expect 0–15%.** It's genuinely workload-dependent and it can be *negative* under memory
bandwidth pressure. Measure. On Windows/MSVC use `_mm_prefetch((const char*)p, _MM_HINT_T0)`;
`__builtin_prefetch` is GCC/Clang — and your CMake toolchain is likely MinGW, so check which
you have.

---

## 6. Where the memory actually goes

For `Key = uint64_t`, `Value = uint64_t`, `LEAF_MAX = INTERNAL_MAX = 64`, N = 1M:

```
 leaves needed  ≈ 1,000,000 / (64 × 0.69)  ≈  22,600     (0.69 = ln 2, typical occupancy)
 internal L1    ≈ 22,600 / 44              ≈     514
 internal L2    ≈ 514 / 44                 ≈      12
 root                                              1
 ─────────────────────────────────────────────────────
 total nodes                               ≈  23,127

 vector node:  ~120 B struct + 3 heap blocks (~1.1 KB payload + ~48 B malloc headers)
 flat node:    ~1.1 KB, one block, no headers

 vector total: ~27 MB    +  ~23,000 × 3 = 69,000 allocations
 flat total:   ~25 MB    +  23,127 allocations
```

Two things:

- **Raw bytes barely differ.** The flat win is *locality and allocation count*, not size.
  Don't argue for it on memory grounds; argue for it on the miss count in §3.
- **Occupancy dominates everything.** At 69% you use 27 MB; at 100% (bulk loaded, §8) you'd
  use ~18 MB. **A third of your memory is the price of supporting incremental insert.** That
  is the honest cost of the structure, and it's the strongest argument for bulk-loading any
  index you build once and read many times — which is exactly your segment model.

> **Consider a node pool.** Since all nodes are the same size, replace `new Node` with a bump
> allocator over big slabs: near-zero allocation cost, no `malloc` header overhead, and nodes
> land near each other in address space (better TLB behaviour). ~40 lines. It also makes
> `clear()` `O(1)` — drop the slabs. Free-list the freed nodes for reuse by splits.

---

## 7. Key compression — the search-engine-specific win

Your keys are **terms**: `std::string`, variable length, and **heavily prefixed** (`laptop`,
`laptops`, `laptop-bag`, `laptop-stand`). Two techniques multiply your fanout, and they apply
to internal nodes especially — where fanout matters most, because those are the nodes that
stay resident.

### 7.1 Suffix truncation (separators only)

From doc 02 §4: a separator only needs to *separate*. If the left subtree's max is `"laptop"`
and the right's min is `"lemon"`, any string in `("laptop", "lemon"]` works — so store
`"le"`, 2 bytes instead of 5.

```cpp
// Shortest string s with left < s <= right. Requires left < right.
std::string shortestSeparator(const std::string& left, const std::string& right) {
    std::size_t i = 0;
    while (i < left.size() && i < right.size() && left[i] == right[i]) ++i;
    return right.substr(0, i + 1);     // common prefix + one distinguishing byte
}
```

Apply it in `splitLeaf`: instead of returning `right->keys.front()`, return
`shortestSeparator(leaf->keys.back(), right->keys.front())`. **The leaf still keeps its full
key** — you're only shortening the copy that goes upstairs.

Verify it preserves I5: `left < s` means everything in the left subtree (all `≤ left`) is
`< s` ✓; `s ≤ right` means everything in the right subtree (all `≥ right`) is `≥ s` ✓.

**Impact:** for English terms, average separator length drops from ~8 bytes to ~3–4.
Internal fanout roughly **doubles**, and internal nodes are the hot ones. This is a large,
cheap, search-engine-specific win, and it's why real term dictionaries do it.

### 7.2 Prefix compression (within a node)

Keys in one node share a long common prefix by construction. Store it once:

```
  raw:        laptop  laptop-bag  laptop-stand  laptops
  compressed: prefix="laptop"  →  ""  "-bag"  "-stand"  "s"
```

Cuts leaf key storage by 50–70% on real term data, roughly doubling `LEAF_MAX`. Costs: a
decompression step on every in-node comparison (though you can compare only the suffix once
you've matched the prefix — often *faster*), and a re-encode on every insert.

**Do 7.1 first.** It's 8 lines, it's pure win, and it targets the nodes that matter most.
7.2 is a bigger change and belongs with the disk format (doc 08 §5), where you're already
serialising and the encode/decode boundary is natural.

---

## 8. Bulk loading — `O(N)`, 100% occupancy, sequential layout

Promised in doc 04 §8.2. This is the highest-value routine in the whole series for your use
case, because your segment model builds indexes from sorted data.

```cpp
// Builds a B+Tree from SORTED, DEDUPLICATED input in one pass.
// fill: fraction of capacity to use. 1.0 for read-only; ~0.7 if inserts will follow.
template <typename It>
void bulkLoad(It first, It last, double fill = 1.0) {
    clear();
    const int leafFill = std::max(1, (int)(LEAF_MAX * fill));
    const int intFill  = std::max(2, (int)(INTERNAL_MAX * fill));

    // ---- level 0: pack the leaves, linking as we go (I7 for free) -----------
    std::vector<Node*> level;
    Node* prev = nullptr;
    for (It it = first; it != last; ) {
        Node* leaf = new Node(/*isLeaf=*/true);
        for (int i = 0; i < leafFill && it != last; ++i, ++it) {
            leaf->keys.push_back(it->first);
            leaf->values.push_back(it->second);
            ++entryCount_;
        }
        if (prev) prev->next = leaf;
        prev = leaf;
        level.push_back(leaf);
    }
    if (level.empty()) { root_ = new Node(true); height_ = 1; return; }

    // ---- fix the LAST node if it fell below the minimum ---------------------
    if (level.size() > 1 && (int)level.back()->keys.size() < LEAF_MIN)
        rebalanceLastTwo(level[level.size() - 2], level.back());   // borrow from the left

    // ---- levels 1..h: pack parents over the level below ---------------------
    height_ = 1;
    while (level.size() > 1) {
        std::vector<Node*> parents;
        for (std::size_t i = 0; i < level.size(); ) {
            Node* p = new Node(/*isLeaf=*/false);
            p->children.push_back(level[i++]);
            while ((int)p->children.size() < intFill && i < level.size()) {
                // separator = smallest key in the child being attached
                p->keys.push_back(firstKeyOf(level[i]));
                p->children.push_back(level[i]);
                ++i;
            }
            parents.push_back(p);
        }
        if (parents.size() > 1 && (int)parents.back()->children.size() < INTERNAL_MIN)
            rebalanceLastTwoInternal(parents[parents.size() - 2], parents.back());
        level.swap(parents);
        ++height_;
    }
    root_ = level.front();
}

// Smallest key in a subtree — for a leaf it's keys[0]; otherwise descend left.
static const Key& firstKeyOf(Node* n) {
    while (!n->isLeaf) n = n->children.front();
    return n->keys.front();
}
```

**The separator rule is `firstKeyOf(rightChild)`** — the same copy-up rule as a leaf split
(doc 04 §3), applied at every level. That's not a coincidence: bulk loading is "do all the
splits at once, in the right order."

**The last-node fix-up is the only fiddly part.** If `N % leafFill == 1`, the final leaf holds
1 entry — below `LEAF_MIN`, breaking I4. Fix by borrowing from the second-to-last (redistribute
the last two nodes evenly) and updating the separator. **Test this specifically:** bulk load
`N = k·leafFill + 1` for several `k`. It's the single edge case in the routine, and it's easy
to miss because it only fires for particular `N`.

**Payoff:**

| | Insert loop | Bulk load |
|---|---|---|
| Time | `O(N log N)`, random access | **`O(N)`**, one pass |
| Occupancy | ~50% (sorted) / ~69% (random) | **100%** (or your chosen fill) |
| Splits | ~N/f | **zero** |
| Layout | Nodes scattered by allocation order | **Sequential** — leaves adjacent in memory/on disk |

That last row matters as much as the first: bulk-loaded leaves are physically adjacent, so a
full scan is sequential — the prefetcher (or disk readahead) runs at full bandwidth. An
insert-built tree's leaves are in allocation order, which is effectively random.

---

## 9. What *not* to optimise

| Tempting | Why it's usually wrong |
|---|---|
| Hand-written SIMD in-node search | The compiler auto-vectorises a simple linear scan at `-O3`. Check the disassembly before writing intrinsics. |
| Threading a single lookup | It's ~200 ns. Thread *across queries*, not within one. |
| Caching recent lookups in the tree | The OS page cache and CPU caches already do this, better. Adds invalidation bugs. |
| Replacing `std::string` keys with interned IDs | Real win — but it's a *schema* change to your engine, not a tree optimisation. Different doc, different decision. |
| Making the tree lock-free | Enormous complexity. Get a `shared_mutex` version working and measure contention first — see `concurrency.md`. |
| Micro-tuning `mid` in splits | Occupancy differences of a few percent, versus the 30%+ available from bulk loading. |

---

## 10. Checkpoint before doc 08

1. Why is fanout 3 wrong for an in-memory tree even though a cache line holds ~3 keys? (§1)
2. Name the four benchmarking mistakes and how each corrupts the result. (§2)
3. Why does a `std::vector`-based node take two dependent cache misses per level? (§3)
4. When does linear in-node search beat binary? Give the mechanism, not the crossover. (§4)
5. `shortestSeparator("laptop", "lemon")` — what does it return, and prove it preserves
   I5. (§7.1)
6. Bulk loading is `O(N)` with 100% occupancy. Why can't incremental insert achieve that? (§8)
7. What's the single edge case in `bulkLoad`, and for which `N` does it fire? (§8)

**Build now:** (1) the fanout sweep from §1 and the harness from §2 — get real numbers for
your machine and write them in a comment at the top of the header; (2) `bulkLoad` plus its
edge-case test; (3) `shortestSeparator` wired into `splitLeaf`, with a test that the tree
still validates and that internal keys really did get shorter. Then, **optionally**, the flat
node layout in §3 as a second header, benchmarked against the first. If it doesn't win on
your data, keeping the vector version is the correct engineering decision — and you'll have
the number that proves it.
