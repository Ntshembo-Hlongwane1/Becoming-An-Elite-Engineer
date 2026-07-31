# 03 — Search & Range Scan

> **The easy doc — read it carefully anyway.** Search is 30 lines and has no rebalancing, so
> it's where you build the routing intuition cheaply, *before* doc 04 makes you maintain that
> routing under structural change. There is exactly one hard thing here: the
> `lower_bound`/`upper_bound` distinction. Get it wrong and your tree loses keys in a way
> that looks like a split bug, and you'll debug the wrong file for a day.

---

## 1. The descent, in one picture

```
 search("owl")

           ┌──────────────────────┐
  root     │  ·  [hen]  ·  [pig] ·│      "owl" ≥ hen ? yes.  "owl" ≥ pig ? no.
           └──┬─────────┬────────┬┘      → take child index 1
              0         1        2
                        │
                        ▼
           ┌──────────────────────┐
  internal │  ·  [ken]  ·  [owl] ·│      "owl" ≥ ken ? yes.  "owl" ≥ owl ? YES (equal!)
           └──┬─────────┬────────┬┘      → take child index 2   ← EQUALITY GOES RIGHT
              0         1        2
                                 │
                                 ▼
           ┌────────────────────────┐
  leaf     │ owl→O   pug→P   rat→R  │    lower_bound("owl") → index 0, keys[0] == "owl" ✓
           └────────────────────────┘    → return &values[0]
```

Three properties fall out, and all three are things a BST cannot offer:

- **Exactly `h` node visits.** Never fewer (you can't stop early — internal nodes hold no
  data, doc 02 §4), never more. Predictable latency.
- **No backtracking.** The descent is a straight line down. Once you commit to a child,
  the range containment invariant (I5) guarantees the key is in that subtree if it's
  anywhere.
- **The failure case costs the same as success.** You land on the leaf that *would* hold the
  key, and it isn't there. That leaf position is exactly what `insert` needs, which is why
  insert reuses this descent verbatim.

---

## 2. The one hard thing: `lower_bound` vs `upper_bound`

Both are `O(log n)` binary searches over a sorted range. They differ only at **equality**:

```
  keys:            [ 10 ][ 20 ][ 30 ][ 40 ]
  indices:            0     1     2     3

  searching for 20:
    lower_bound → first element NOT LESS than 20  → index 1   (points AT the 20)
    upper_bound → first element GREATER than 20   → index 2   (points PAST the 20)

  searching for 25 (absent):
    lower_bound → index 2       both agree when the key is absent
    upper_bound → index 2
```

Now map that onto the two node kinds. This table is the doc:

| Node kind | Which function | Why | What the result means |
|---|---|---|---|
| **Internal** | `upper_bound` | Separator semantics: key `== kᵢ` belongs to the **right** child (doc 01 §4) | The **child index** to descend into |
| **Leaf** | `lower_bound` | You want the position **of** the key, or where it would go | The **entry index**; check `idx < n && keys[idx] == key` for a hit |

### Why internal uses `upper_bound` — derived, not memorised

The routing rule is: descend into child `i` where `k_{i-1} ≤ x < kᵢ`. So `i` is the index of
the **first separator strictly greater than `x`**. "First element greater than x" is the
definition of `upper_bound`. Done.

Check the boundaries, which is where an incorrect choice shows up:

```
  keys:      [hen][pig]        children: c₀ c₁ c₂

  x = "ant"  → upper_bound = 0 → c₀ ✓   (ant < hen, leftmost subtree)
  x = "hen"  → upper_bound = 1 → c₁ ✓   (equality goes right — correct!)
  x = "owl"  → upper_bound = 1 → c₁ ✓   (hen ≤ owl < pig)
  x = "pig"  → upper_bound = 2 → c₂ ✓   (equality goes right)
  x = "zoo"  → upper_bound = 2 → c₂ ✓   (rightmost subtree)
```

`upper_bound` returns a value in `[0, n]` — exactly the valid child index range for `n+1`
children. That's not a coincidence; it's invariant I2 and the routing rule agreeing.

> **The bug you avoid.** Use `lower_bound` in an internal node and `x == "hen"` routes to
> `c₀`. But `insert` (doc 04) puts `hen` in `c₁` — it *has* to, because leaf splits copy up
> `rightLeaf.keys[0]`, making the separator equal to a key that lives on the right. So you
> insert `hen`, search for `hen`, and get "not found." You will suspect your split code.
> The split code is fine. **Symptom to remember: only keys that happen to equal a separator
> go missing.** If exactly the keys that appear in internal nodes are unfindable, it's this.

### Write them yourself once

`std::lower_bound` / `std::upper_bound` are the right answer in production, but implement
both once by hand — the invariant is subtle and you'll want to have felt it, and doc 07 §4
replaces these with a branchless variant anyway.

```cpp
// First index i in [0, n) with keys[i] >= key.  Returns n if none.
template <typename Key, typename Compare>
int lowerBoundIdx(const std::vector<Key>& keys, const Key& key, Compare less) {
    int lo = 0, hi = static_cast<int>(keys.size());     // invariant: answer in [lo, hi]
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;                   // no overflow
        if (less(keys[mid], key)) lo = mid + 1;         // keys[mid] < key → answer is right
        else                      hi = mid;             // keys[mid] >= key → mid is a candidate
    }
    return lo;
}

// First index i in [0, n) with keys[i] > key.  Returns n if none.
template <typename Key, typename Compare>
int upperBoundIdx(const std::vector<Key>& keys, const Key& key, Compare less) {
    int lo = 0, hi = static_cast<int>(keys.size());
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (less(key, keys[mid])) hi = mid;             // key < keys[mid] → mid is a candidate
        else                      lo = mid + 1;         // keys[mid] <= key → answer is right
    }
    return lo;
}
```

Note the **only** difference: `less(keys[mid], key)` vs `less(key, keys[mid])` — the operands
are swapped. That's how `upper_bound` turns "not less" into "greater." Everything else is
identical. Also note: both use **only `<`**, never `==` or `>`. That's deliberate — see §3.

---

## 3. Comparators: use strict-weak-ordering, only `<`

Your tree should take a comparator like the standard library does:

```cpp
template <typename Key, typename Value, typename Compare = std::less<Key>>
class BPlusTree { ... };
```

and **derive everything from `less`**:

```cpp
bool eq(const Key& a, const Key& b) const { return !less(a, b) && !less(b, a); }
bool gt(const Key& a, const Key& b) const { return less(b, a); }
bool ge(const Key& a, const Key& b) const { return !less(a, b); }
```

Why bother instead of using `==` and `>`?

1. **One source of truth.** If the caller passes a case-insensitive comparator, `a == b`
   using `operator==` disagrees with the ordering, and your tree silently corrupts: `insert`
   routes by `less` but detects duplicates by `==`, so you get two entries the tree thinks
   are distinct but the ordering thinks are equal. I1 breaks.
2. **It's the STL contract.** Users expect `Compare` alone to define equivalence.
3. **A type may not have `operator==`.** Requiring only `<` is the weaker, better constraint.

> **For your term dictionary:** `Key = std::string` with `std::less<std::string>` gives
> byte-lexicographic order. That's what you want — it makes prefix queries a *contiguous
> range* (§6), because all strings starting with `"lap"` sort together between `"lap"` and
> the next string after the prefix. If you ever want case-insensitive or locale-aware
> collation, that's a comparator change, and *nothing else in the tree changes*. That's the
> payoff for routing all comparisons through `less`.

---

## 4. `search` — the code

```cpp
// Returns pointer to the stored value, or nullptr if absent.
// Non-owning; invalidated by any subsequent insert/remove.
Value* search(const Key& key) {
    Node* node = root;
    if (!node) return nullptr;

    // ---- descend: internal nodes route, they never answer ------------------
    while (!node->isLeaf) {
        int i = upperBoundIdx(node->keys, key, less);   // ← upper_bound (§2)
        node = node->children[i];
    }

    // ---- the leaf answers ---------------------------------------------------
    int i = lowerBoundIdx(node->keys, key, less);       // ← lower_bound (§2)
    if (i < (int)node->keys.size() && eq(node->keys[i], key))
        return &node->values[i];
    return nullptr;
}
```

That's the whole point lookup. Things to notice:

- **The `while` is not recursion.** Descent is a loop — no stack frames, no recursion depth
  concern, and it's tail-recursive anyway so recursion would buy nothing. (Insert and delete
  *do* benefit from recursion, doc 04 §6, because they need the unwind.)
- **`node->children[i]` is never out of range** by I2 + the range of `upperBoundIdx`. If it
  can be, I2 is broken and the validator will have told you.
- **Returning `Value*`** exposes the internal storage so callers can mutate in place. That's
  useful (update a posting-list offset without a re-descent) but it's a **borrowed
  reference** — any insert can split the leaf and reallocate the vector, dangling it. Document
  that. This is exactly the ownership/lifetime discipline from your
  `ownership-and-lifecycle-*` docs; the safe alternative is returning `std::optional<Value>`
  by value, at the cost of a copy.

### The variant insert needs

Insert needs the same descent *plus* the path. Factor it out so there's one routing
implementation — if routing logic gets duplicated between `search` and `insert`, they will
eventually disagree, and that bug presents as "some keys are unfindable."

```cpp
struct Descent {
    Node*                            leaf;
    std::vector<std::pair<Node*,int>> path;  // (internal node, child index taken)
};

Descent descendToLeaf(const Key& key) {
    Descent d;
    Node* node = root;
    while (!node->isLeaf) {
        int i = upperBoundIdx(node->keys, key, less);
        d.path.emplace_back(node, i);
        node = node->children[i];
    }
    d.leaf = node;
    return d;
}
```

`search` becomes `descendToLeaf(key).leaf` + the leaf probe. Doc 04's insert unwinds
`d.path`. Doc 05's remove does too. **One routing implementation, three users.**

---

## 5. Range scan — the reason you chose B+Tree

`scan(lo, hi)` yields every entry with `lo ≤ key < hi`, in ascending order.

```
 scan("dog", "rat")

  1. descend once to the leaf that would contain "dog"           ← h node visits
  2. lower_bound within that leaf → start index
  3. walk forward through entries; at the end of a leaf, follow `next`
  4. stop at the first key >= "rat"

   ┌──────────────┐   ┌──────────────┐   ┌──────────────┐
   │ ant cat dog  │──▶│ eel fox hen  │──▶│ owl pig rat  │──▶ ...
   └──────┬───────┘   └──────────────┘   └────────┬─────┘
          │ start here                            │ stop here
          └───────────── sequential ──────────────┘
```

**Cost: `O(h + k)`** for `k` results — one descent, then pure sequential work. Contrast the
BST: an in-order traversal from a start key climbs up and down internal nodes repeatedly,
`O(log N)` amortised per step with terrible locality. And contrast a hash map: **impossible**
— hashing destroys order, so a range query is a full scan of the whole table.

```cpp
template <typename Fn>
void scan(const Key& lo, const Key& hi, Fn&& visit) {
    Node* node = descendToLeaf(lo).leaf;
    int   i    = lowerBoundIdx(node->keys, lo, less);

    while (node) {
        for (; i < (int)node->keys.size(); ++i) {
            if (!less(node->keys[i], hi)) return;    // keys[i] >= hi → done
            visit(node->keys[i], node->values[i]);
        }
        node = node->next;   // ← invariant I7 is what makes this line correct
        i    = 0;
    }
}
```

Six lines of loop, and it is the fastest ordered scan any structure gives you. Note the
half-open `[lo, hi)` convention — match the STL, and every off-by-one at the boundary
disappears.

> **This is where I7 earns its keep.** If a split forgot to relink `next`, this scan silently
> returns *partial* results — no crash, no assert, just missing data. That is the worst
> failure mode there is. Which is why doc 02 §6's validator checks the chain, and doc 09's
> test suite scans after every mutation.

---

## 6. Prefix search — the search-engine payoff

Prefix query `"lap*"` is a range query, because lexicographic order puts every string
starting with `lap` in one contiguous run:

```
  ... lantern | lap | lapdog | laptop | laptops | lard | large ...
                └──────── all keys with prefix "lap" ────────┘
                 lo = "lap"                    hi = "laq"
```

The upper bound is the prefix with its **last byte incremented**:

```cpp
// Smallest string strictly greater than every string having `prefix`.
// Returns nullopt when the prefix is all 0xFF (scan to end of tree).
std::optional<std::string> prefixUpperBound(std::string prefix) {
    while (!prefix.empty()) {
        auto& back = reinterpret_cast<unsigned char&>(prefix.back());
        if (back != 0xFF) { ++back; return prefix; }
        prefix.pop_back();                       // 0xFF carries: "ab\xFF" → "ac"
    }
    return std::nullopt;
}

template <typename Fn>
void scanPrefix(const std::string& prefix, Fn&& visit) {
    auto hi = prefixUpperBound(prefix);
    if (hi) scan(prefix, *hi, visit);
    else    scanToEnd(prefix, visit);
}
```

Cost: `O(h + k)`. Three node visits, then stream `k` matches sequentially. **This is the
operation your `unordered_map<string, vector<string>>` in `internal/store` cannot do at
all** — it would require hashing every possible completion, or a full scan of every term.
Frontier doc 05 calls this "query rewriting: prefixes expand via an ordered term structure."
This is that structure, and this function is that expansion.

> **Byte-level caution:** the carry loop matters. Naïve `prefix.back()++` on `"ab\xFF"`
> overflows to `"ab\x00"`, which sorts *below* the prefix — your scan returns nothing.
> Also: use `unsigned char`. `char` is signed on x86, so `0x80..0xFF` compare as negative and
> your ordering breaks on any non-ASCII term. If your terms can hold UTF-8 — and for a real
> search engine they will — this is a live bug, not a theoretical one.

---

## 7. Iterators — worth adding, cheap to do

Once the leaf chain exists, an STL-style forward iterator is ~40 lines and makes the tree
work with range-for, `std::copy`, `std::accumulate`, and every algorithm you already know.

```cpp
class iterator {
    Node* leaf = nullptr;
    int   idx  = 0;
public:
    using iterator_category = std::forward_iterator_tag;
    using value_type        = std::pair<const Key&, Value&>;
    using difference_type   = std::ptrdiff_t;

    value_type operator*() const { return { leaf->keys[idx], leaf->values[idx] }; }

    iterator& operator++() {
        if (++idx >= (int)leaf->keys.size()) { leaf = leaf->next; idx = 0; }
        return *this;
    }
    iterator operator++(int) { auto t = *this; ++*this; return t; }

    bool operator==(const iterator& o) const { return leaf == o.leaf && idx == o.idx; }
    bool operator!=(const iterator& o) const { return !(*this == o); }
};

iterator begin() { Node* n = root; while (n && !n->isLeaf) n = n->children.front(); return {n, 0}; }
iterator end()   { return {nullptr, 0}; }
iterator lowerBound(const Key& k) {                       // ← the useful one
    Node* n = descendToLeaf(k).leaf;
    int   i = lowerBoundIdx(n->keys, k, less);
    if (i >= (int)n->keys.size()) return {n->next, 0};    // spilled past this leaf's end
    return {n, i};
}
```

Two subtleties, both real bugs if missed:

- **`operator++` must skip empty leaves** if your delete policy allows them (lazy delete does).
  `if (++idx >= size) { leaf = leaf->next; idx = 0; }` stops at an empty leaf with `idx = 0`,
  and `operator*` then reads `keys[0]` out of bounds. Make it a `while` over empty leaves.
- **`lowerBound` must handle "past the end of this leaf."** If `k` is greater than every key
  in the landing leaf, the answer is the *next* leaf's first entry — the `{n->next, 0}` line.
  Forget it and `lowerBound` returns an out-of-range iterator.

**Only forward, not bidirectional** — with a singly-linked chain (doc 02 §3) you cannot
implement `operator--`, so declare `forward_iterator_tag` honestly. Lying to the STL about
your category produces algorithms that compile and then do wrong things.

---

## 8. Cost summary

| Operation | Cost | Dominated by |
|---|---|---|
| `search(k)` | `O(log_f N)` node visits × `O(log₂ f)` in-node | ~3–4 cache misses / disk reads |
| `scan(lo, hi)` → k results | `O(log_f N + k)` | Sequential leaf walk — prefetcher-friendly |
| `scanPrefix(p)` → k results | `O(log_f N + k)` | Same |
| `begin()..end()` full scan | `O(N/L)` node visits | Pure sequential, ~L entries per visit |
| `lowerBound(k)` | `O(log_f N)` | Same as search |

Note what's **absent**: no operation backtracks, and none is worse than `O(log N + output)`.
That uniformity is the structural property you bought.

---

## 9. Checkpoint before doc 04

1. Internal nodes use `upper_bound`, leaves use `lower_bound`. Derive both from the routing
   rule — don't recite. (§2)
2. You use `lower_bound` in internal nodes by mistake. Describe the *exact* symptom a user
   sees. (§2)
3. Why must `eq()` be built from `less()` rather than `operator==`? Give a comparator where
   the difference corrupts the tree. (§3)
4. Why is `scan` `O(h + k)` and not `O(k log N)`? Which invariant is load-bearing? (§5)
5. Write `prefixUpperBound("az\xFF\xFF")` by hand. What's the answer? (§6)
6. Why is the iterator `forward_iterator_tag` and not `bidirectional`? (§7)

**Build now:** `search()`, `scan()`, `scanPrefix()`, and the iterator — against a
**hand-built** tree (you can't insert yet). Wire up 3 leaves and 1 root manually in a test,
run `validate()`, then verify: every key findable, an absent key returns `nullptr`, a key
equal to a separator is findable (**this is the test that catches the `upper_bound` bug**),
a scan crossing all three leaves returns everything in order, and a prefix scan returns
exactly the right contiguous run. Doing this before insert exists means that when insert
breaks something in doc 04, you *know* search isn't the culprit.
