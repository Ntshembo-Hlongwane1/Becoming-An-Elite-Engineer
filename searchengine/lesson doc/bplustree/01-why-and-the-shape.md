# 01 — Why B+Tree, and the Shape

> **Start where you are.** You have a working `bstree.hpp`. It is correct. It is also, for
> any dataset that doesn't fit in L2 cache, roughly the worst possible shape for the
> hardware. This doc explains exactly why, derives the fix from first principles, and lands
> on the B+Tree's shape as the *inevitable* answer rather than an arbitrary one.

---

## 1. The diagnosis: what's wrong with your BST

Here's your node, from `internal/kernal/core/datastructures/bstree.hpp:5`:

```cpp
class TreeNode {
public:
    std::string value;              // 32 bytes (libstdc++ SSO string)
    TreeNode* leftChild = nullptr;  // 8 bytes
    TreeNode* rightChild = nullptr; // 8 bytes
};
```

48 bytes, individually `new`'d. Three separate problems, in increasing order of severity.

### Problem 1 — It isn't balanced

`recurseInsertion` does no rebalancing. Insert your inventory terms in sorted order — which
is *exactly* what happens if you ever build the index from a sorted term list — and you get
a linked list:

```
 "apple" → "banana" → "cherry" → "date" → ...     height = N
```

Search becomes O(N). Not "O(log N) with a bad constant." Linear. This is the textbook
failure, and it's the *least* important one on this list.

### Problem 2 — Each node visit is a cache miss

Assume the tree *is* balanced. 1,000,000 terms → height ≈ 20. Each step of `recurseSearch`
dereferences a pointer to a node allocated at some unrelated address. From frontier doc 01,
the cost table you already know:

```
 L1 hit        ~1  ns
 L2 hit        ~4  ns
 L3 hit        ~15 ns
 DRAM          ~80 ns      ← where a random 48-byte node lives
 NVMe read     ~100 µs
```

Twenty pointer chases, each a likely DRAM round-trip, and **each one is dependent on the
previous** — the CPU cannot prefetch node `k+1` until it has loaded node `k` and compared
the string. This is a *pointer-chasing dependency chain*: 20 × 80 ns ≈ **1.6 µs of pure
stall** for one lookup, with the CPU's out-of-order machinery unable to help at all.

Now the insult: the hardware loaded a **64-byte cache line** for each node. Your node used
48 bytes of it, of which the search only needed the string — 16 or so bytes. You paid a
full DRAM transaction for ~25% useful payload, **twenty times**.

### Problem 3 — It cannot be put on disk

`TreeNode*` is a process-local virtual address. Write it to a file and it is meaningless on
the next run. To persist a BST you'd have to convert every pointer into a file offset
(frontier doc 02 §3) — and then a 20-level tree becomes **20 disk reads**, ~100 µs each =
**2 ms per lookup**. A B+Tree of the same data is 3 reads, and 2 of those 3 are almost
certainly in the page cache.

> **The unifying observation:** the BST's node is *too small*. Every design failure above is
> a consequence of putting **one key** behind **one pointer chase**, when the hardware
> charges you for 64 bytes (cache line) or 4096 bytes (page) whether you use them or not.

---

## 2. The fix, derived rather than announced

The hardware's unit of transfer is fixed: 64 bytes to cache, 4096 bytes from disk. You are
billed per *transfer*, not per byte. So the optimisation is forced:

> **Maximise the useful work done per transfer.**

For a search tree, "useful work" means "keys compared" — because each comparison is what
narrows the search space. So:

- BST: 1 key per transfer → each transfer eliminates **half** the remaining space →
  `log₂(N)` transfers.
- If a node held `f` keys, one transfer eliminates **(f)/(f+1)** of the space → `log_f(N)`
  transfers.

The change of base is the entire idea:

```
                log₂(N)
 log_f(N)  =   ─────────
                log₂(f)
```

Height shrinks by a factor of `log₂(f)`. Concretely, with `f = 200`, `log₂(200) ≈ 7.6`:
a 20-level BST becomes a **2.6-level** tree. That's the whole payoff, and it comes from one
decision: **make the node the size of the transfer unit.**

### The fanout arithmetic — do this on paper

Node = one 4096-byte page. Keys are 16-byte fixed-size terms, child pointers/page-ids are
8 bytes. An internal node with `c` children holds `c-1` separator keys:

```
 header (type, count, checksum, ...)     ≈    32 bytes
 (c-1) keys × 16 bytes
 c pointers × 8 bytes
 ────────────────────────────────────────────────────
 32 + 16(c-1) + 8c  ≤  4096
 32 + 16c - 16 + 8c ≤  4096
 24c                ≤  4080
 c                  ≤  170
```

**Fanout ≈ 170.** Now count what a tree of height `h` can address, assuming leaves hold
~120 entries each (leaves store key+payload, so fewer fit):

| Height | Leaf nodes | Entries addressable | Reads per lookup |
|---|---|---|---|
| 1 (root is a leaf) | 1 | 120 | 1 |
| 2 | 170 | ~20,400 | 2 |
| 3 | 28,900 | ~3.5 million | 3 |
| 4 | 4,913,000 | ~589 million | 4 |

**Three levels holds 3.5 million terms.** And the root — 4 KB — is *permanently* in L1/L2
after the first query. Level 2 is 170 pages = 680 KB, which lives comfortably in L3 or the
page cache. So a lookup in a 3.5-million-term dictionary is, in practice, **two cached
touches and one real memory/disk access.**

Compare against the BST: 20 dependent DRAM stalls. That is the ~10× that makes this
structure the default for every database and filesystem ever shipped.

> **Your turn (5 minutes, on paper):** redo the fanout calculation for *your* case — variable
> length terms averaging 12 bytes, 8-byte page-ids, 4 KB pages. Then redo it for a 64-byte
> **cache line** instead of a page, which is the right unit for a purely in-memory tree. The
> two answers are very different (roughly 170 vs 3) and doc 07 §2 explains why the in-memory
> answer is *not* "use fanout 3."

---

## 3. B-Tree vs B+Tree — the one difference that matters

Both are balanced n-ary trees with the same split/merge machinery. The difference is
**where values live**.

### B-Tree: data in every node

```
                    ┌────────────────────┐
                    │ [dog|→D] [pig|→P]  │       ← internal node HOLDS payloads
                    └──┬──────────┬────┬─┘
              ┌────────┘          │    └────────┐
   ┌──────────────────┐  ┌────────────────┐  ┌──────────────────┐
   │ [ant|→A][cat|→C] │  │ [fox|→F][hen|H]│  │ [rat|→R][yak|→Y] │
   └──────────────────┘  └────────────────┘  └──────────────────┘
```

- A lookup can terminate **early** — find `"dog"` at the root, done, 1 read.
- But payloads in internal nodes **steal space from keys**, so fanout collapses. If a
  payload is 100 bytes, our 4 KB node holds ~35 keys instead of 170. Height goes from 3 to
  4–5 for the same data.
- Range scans are **awful**: an in-order traversal must walk *up and down* the tree
  repeatedly, re-visiting internal nodes.

### B+Tree: data only in leaves, leaves linked

```
                    ┌──────────────┐
                    │  [dog] [pig] │              ← internal: SEPARATORS ONLY, no payload
                    └──┬───────┬──┬┘
              ┌────────┘       │  └────────────┐
   ┌────────────────┐   ┌───────────────┐   ┌────────────────┐
   │ ant→A  cat→C   │──▶│ dog→D  hen→H  │──▶│ pig→P  rat→R   │   ← leaves: all data
   └────────────────┘   └───────────────┘   └────────────────┘
        ▲ linked list of leaves, left to right, in key order ▲
```

Note `dog` appears **twice** — once as a separator upstairs, once as real data downstairs.
That duplication is deliberate and is the source of every nice property below.

| Property | B-Tree | B+Tree |
|---|---|---|
| Fanout | Reduced by payload size | Maximal — keys + pointers only |
| Height | Higher | Lower |
| Lookup cost | 1..h reads (variable) | Always exactly `h` (predictable) |
| Range scan | Traverse up/down repeatedly | One descent + linked-list walk |
| Full scan | Random-ish order | Sequential over leaves |
| Delete | Complex (may delete from internal node, needs predecessor swap) | Simpler (always delete from a leaf) |
| Cache behaviour of internals | Polluted with payloads | Dense, hot, stays resident |

**The decisive one for you is the range scan.** Your search engine's core operations —
prefix queries (`"lap*"` → every term from `lap` to `lapz`), range filters on numeric
fields, iterating a term dictionary to merge segments — are all *ranges*. In a B+Tree that
is: descend once (3 reads), then walk the leaf chain sequentially, which the hardware
prefetcher loves and which streams from disk at full bandwidth.

> **The "always exactly h" row is underrated.** Predictable latency is worth real money in a
> search engine: your p99 equals your p50. A B-Tree's variable termination depth means the
> tail is genuinely worse even though the average is better.

---

## 4. The shape, precisely

```
 level 0 (root, internal)
 ┌───────────────────────────────┐
 │  ·  [ hen ]  ·  [ pig ]  ·    │      3 children, 2 separator keys
 └──┬───────────┬──────────┬─────┘
    │           │          │
    ▼           ▼          ▼
 level 1 (leaves)
 ┌──────────┐  ┌──────────┐  ┌──────────┐
 │ ant→A    │  │ hen→H    │  │ pig→P    │
 │ cat→C    │─▶│ dog... ✗ │─▶│ rat→R    │─▶ nullptr
 │ eel→E    │  │ owl→O    │  │ yak→Y    │
 └──────────┘  └──────────┘  └──────────┘
   keys < hen    hen ≤ k < pig   pig ≤ keys
```

Read the routing rule off that picture, because you will implement it in doc 03 and it is
the single most common place to introduce an off-by-one:

> For an internal node with keys `k₀ < k₁ < ... < k_{n-1}` and children `c₀ ... c_n`:
> **subtree `cᵢ` contains exactly the keys `x` with `k_{i-1} ≤ x < kᵢ`** (with `k₋₁ = -∞`
> and `k_n = +∞`).

Two consequences to burn in now:

1. **The separator is a lower bound on the right child, not an upper bound on the left.**
   A key equal to the separator goes **right**. (`hen` lives in the middle leaf, not the
   left one.) This is why in-node routing uses `upper_bound`, not `lower_bound` — doc 03 §2.
2. **A separator need not be a key that exists in the tree.** Delete `hen` from the middle
   leaf and the separator `hen` stays put, still routing correctly — everything `≥ hen` is
   still in the middle-or-right subtree. This one fact removes an entire class of work from
   the delete algorithm, and beginners waste days "fixing" separators that were never
   broken. Doc 05 §6.

---

## 5. Naming: order, fanout, and why the textbooks disagree

This is a genuine trap. "Order" is used to mean at least three different things (Knuth's
order = max children; some texts use minimum degree `t`; others count keys). You will read
two sources, get contradictory `⌈m/2⌉` formulas, and write an off-by-one bug.

**Kill the ambiguity by never using the word "order" in your code.** Use these, and copy
them into your header verbatim:

```cpp
// Maximum number of CHILDREN an internal node may have.
// An internal node with C children has exactly C-1 separator keys.
static constexpr int INTERNAL_MAX_CHILDREN = 4;

// Minimum children for a NON-ROOT internal node. Root may have as few as 2.
static constexpr int INTERNAL_MIN_CHILDREN = (INTERNAL_MAX_CHILDREN + 1) / 2;  // ceil

// Maximum number of key→value ENTRIES a leaf may hold.
static constexpr int LEAF_MAX_ENTRIES = 4;

// Minimum entries for a NON-ROOT leaf. Root-as-leaf may hold as few as 1 (or 0 if empty).
static constexpr int LEAF_MIN_ENTRIES = (LEAF_MAX_ENTRIES + 1) / 2;            // ceil
```

Four things worth noticing:

- **Internal and leaf capacities are separate constants.** They *must* be, in any serious
  implementation: an internal entry is `key + pointer`, a leaf entry is `key + value`, and
  those are different sizes. Real systems size each so that its node fills one page. Most
  tutorials conflate them and then can't explain why their disk pages are half empty.
- `(x + 1) / 2` is integer `ceil(x/2)`. Write it that way; `ceil(x/2.0)` in a `constexpr`
  is asking for a floating-point surprise.
- **The root is exempt from the minimum.** A tree with 1 entry is a single leaf holding
  1 entry, which is below `LEAF_MIN_ENTRIES`. That's legal and necessary. Every invariant
  check must special-case the root — doc 02 §5, invariant I4.
- **Start with 4.** Small enough that a 20-key test forces multiple splits, a root split,
  and a merge — all traceable by hand on one sheet of paper. You cannot debug a split with
  fanout 170; you cannot benchmark with fanout 4. Doc 07 §1 makes it a template parameter so
  you get both.

---

## 6. Height is a *guarantee*, not an average

The BST is balanced "if you're lucky." The B+Tree is balanced by construction, and the
mechanism is worth stating explicitly because it's unusual:

> **A B+Tree never grows downward. It grows upward, at the root.**

Every node is created by a **split**, and a split produces two siblings *at the same level*
as the node it replaced. The only operation that increases height is the **root split**:
when the root itself overflows, a brand-new root is created above it with exactly two
children. Because that happens at the top, **every leaf moves down by exactly one level at
the same instant**.

Therefore: *all leaves are always at the same depth*. Not "approximately," not "within a
factor of two" (as in a red-black tree) — **exactly**. Deletion works the same way in
reverse: merges shrink nodes, and only the collapse of a 1-child root reduces height, again
for all leaves at once.

The bound follows mechanically. With `N` entries, minimum leaf occupancy `L_min` and minimum
internal fanout `F_min`:

```
 h  ≤  1 + log_{F_min}( N / L_min )
```

For `F_min = 85`, `L_min = 60`, `N = 10⁶`: `h ≤ 1 + log₈₅(16666) ≈ 1 + 2.2 = 3.2` → **4
levels, worst case, guaranteed.** Not on average. Not if the insert order is nice. Always.

That guarantee is the product being sold. Everything in docs 04 and 05 — the fiddly split
arithmetic, the borrow-vs-merge decision tree — exists solely to maintain it.

---

## 7. What this costs you

Be honest about the trade, so you know when *not* to reach for this:

| Cost | Detail |
|---|---|
| Implementation complexity | ~600 lines vs ~100 for a BST. Delete is genuinely hard. Doc 05 exists because of this. |
| Space overhead | Nodes are only guaranteed half full. Average occupancy under random inserts is ~69% (`ln 2`). You waste ~30% of your allocated space. |
| Bad at pure point-lookup-only workloads | If you *never* range scan and everything fits in RAM, a good hash table beats it — O(1) vs O(log_f N). Databases use B+Trees because they need ordering; if you don't, don't. |
| Write amplification | One inserted key can trigger a split cascade up to the root, rewriting `h` nodes. LSM-trees (doc 08 §9) trade read cost for write cost precisely to avoid this. |

> **The honest summary:** you want a B+Tree when your data is **ordered**, **larger than
> cache**, and **range-queried**. A term dictionary is all three. Your `unordered_map<string,
> vector<string>>` in `internal/store` is none of them *yet* — but the moment you need
> `"lap*"` prefix queries or on-disk terms, it becomes all three at once.

---

## 8. Checkpoint before doc 02

Answer these without scrolling up. If any is shaky, re-read the linked section.

1. A BST and a B+Tree both hold 1M keys and both are perfectly balanced. Why is the B+Tree
   still ~7× faster for a point lookup? (§1, §2)
2. Your leaf entries are `key(16B) + value(8B)` and your pages are 4 KB. What's
   `LEAF_MAX_ENTRIES`? What's `INTERNAL_MAX_CHILDREN` for a 16-byte key and 8-byte page-id?
   (§2)
3. Key `hen` equals a separator in an internal node. Which child do you descend into? (§4)
4. Why does a B+Tree separator not need to be a key that exists in the tree? (§4)
5. Why can't a B+Tree ever become unbalanced, stated as a mechanism, not a claim? (§6)
6. Name a workload where you should *not* use a B+Tree, and say what to use instead. (§7)

**Write this down before continuing** — it's the spec for doc 02: sketch, on paper, a
B+Tree with `INTERNAL_MAX_CHILDREN = 4` and `LEAF_MAX_ENTRIES = 4` after inserting the keys
`10, 20, 30, 40, 50, 60, 70` in that order. You won't get it right yet — the split rules
come in doc 04 — but commit to a guess. Doc 04 §7 traces exactly this sequence, and
comparing it against your guess is worth more than reading the trace cold.
