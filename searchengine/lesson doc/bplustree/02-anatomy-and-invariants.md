# 02 — Anatomy & Invariants

> **This is the reference doc.** Docs 03–09 cite the invariants here by number (I1..I8).
> The single highest-leverage thing you will build in this entire series is not `insert` —
> it's the **validator** in §6, which checks all eight invariants against a live tree. With
> it, every bug in split and merge becomes a two-minute fix. Without it, they become a
> two-day fix. Build the validator *before* you build insert.

---

## 1. Two node kinds, one type — the first design decision

A B+Tree has internal nodes (keys + children) and leaf nodes (keys + values + next-pointer).
C++ gives you four ways to express that. You will meet all four in real code, so know the
trade before you pick.

### Option A — One struct with a flag *(recommended for v1)*

```cpp
template <typename Key, typename Value>
struct Node {
    bool isLeaf;
    std::vector<Key>    keys;

    std::vector<Node*>  children;  // internal only: size() == keys.size() + 1
    std::vector<Value>  values;    // leaf only:     size() == keys.size()
    Node*               next;      // leaf only: right sibling, nullptr at the end
};
```

- **Pro:** trivially simple; one allocation; `isLeaf` branch is perfectly predicted (the
  branch resolves the same way for all nodes at a given level of a descent).
- **Con:** every leaf carries an empty `children` vector (24 wasted bytes) and every internal
  carries empty `values` + `next` (32 wasted). ~56 bytes of waste per node.
- **Verdict:** start here. The waste is irrelevant at fanout 4 and it lets you write and
  debug the algorithms without also fighting a type hierarchy. Doc 07 §3 shows the upgrade.

### Option B — Base class + virtual

```cpp
struct NodeBase { bool isLeaf; std::vector<Key> keys; virtual ~NodeBase() = default; };
struct Internal : NodeBase { std::vector<NodeBase*> children; };
struct Leaf     : NodeBase { std::vector<Value> values; Leaf* next; };
```

- **Pro:** no wasted members; types express intent.
- **Con:** vtable pointer (8 bytes) in *every* node, `dynamic_cast`/`static_cast` at every
  descent step, and — the real killer — the vtable makes the node **non-trivially-copyable**,
  so you can never `memcpy` it to a disk page. Doc 08 depends on being able to do exactly
  that.
- **Verdict:** avoid. Virtual dispatch is the wrong tool here; the "polymorphism" is one
  bool, resolved once per level.

### Option C — Union / `std::variant` payload

Saves the memory of A without the vtable of B, at the cost of `std::get`/`visit` noise
everywhere. Fine, but the ceremony buys little over A.

### Option D — Fixed-size arrays, no heap-allocated members *(the endgame)*

```cpp
template <typename Key, typename Value, int MAX>
struct Node {
    uint16_t count;
    bool     isLeaf;
    Key      keys[MAX];
    union { Node* children[MAX + 1]; Value values[MAX]; };
    Node*    next;
};
```

- One contiguous block. No pointer-chase to reach the keys (a `std::vector`'s data lives
  *elsewhere* — see the trap below). Trivially copyable → `memcpy`-able to a page.
- This is what every production B+Tree looks like. Doc 07 §3 walks the migration.

> **The `std::vector` trap — understand this now, it undercuts §2 of doc 01.**
> `std::vector<Key> keys` is 24 bytes *in the node*: a pointer, a size, a capacity. The
> actual keys live in a **separate heap allocation**. So loading the node into cache does
> **not** load its keys — you take a *second* cache miss to reach them. A vector-based
> B+Tree therefore has **two** misses per level instead of one, halving the advantage you
> derived in doc 01 §2. It's still far better than a BST (2 misses × 3 levels = 6, vs 20),
> but it is not the real thing. Option D is the real thing. Build A, measure it, then
> migrate to D and measure again — doc 07 §6 makes this an explicit experiment.

---

## 2. The internal node, anatomically

```
 Internal node, INTERNAL_MAX_CHILDREN = 4  (so at most 3 keys)

   keys:      [  hen  ][  pig  ][  yak  ]         n = 3 keys
   children:  c₀   c₁       c₂       c₃           n+1 = 4 children
              │    │        │        │
              │    │        │        └──▶ subtree: yak ≤ x
              │    │        └───────────▶ subtree: pig ≤ x < yak
              │    └────────────────────▶ subtree: hen ≤ x < pig
              └─────────────────────────▶ subtree:       x < hen
```

**The `n+1` relationship is the invariant you will break most often** (I2 below). Every
split and every merge must preserve it, and an off-by-one here produces a tree that *looks*
fine on a small test and segfaults on the 200th insert. The validator catches it instantly.

A mental model that prevents the off-by-one: think of the keys as **fenceposts between
children**, not as data.

```
  c₀ | hen | c₁ | pig | c₂ | yak | c₃
```

4 children, 3 fences between them. You cannot have 4 fences between 4 posts. Draw this
whenever you're editing a `children` vector.

**Internal nodes carry no values.** Not even for keys that exist in the tree. If your
internal node has a `values` field that's ever read, you've built a B-Tree.

---

## 3. The leaf node, anatomically

```
 Leaf node, LEAF_MAX_ENTRIES = 4

   keys:    [ hen ][ owl ][ pig ]         n = 3
   values:  [  H  ][  O  ][  P  ]         n = 3   ← parallel arrays, same index
   next:    ──────────────────────▶ (the next leaf in key order)
```

Three things:

1. **`keys.size() == values.size()`** in a leaf, always (I3). Not `+1`. Leaves have no
   children, so there are no fences — key `i` and value `i` are one logical entry.
2. **`next` is a singly-linked list over all leaves, in ascending key order** (I7). Building
   it correctly is a two-line detail during split that people forget, and the bug only shows
   up in range scans — which is why doc 09's test suite scans the chain after *every*
   operation.
3. **Parallel arrays, not `vector<pair<Key,Value>>`.** Why: search only touches keys. With
   parallel arrays, a cache line filled with keys holds ~4 × 16-byte keys. With
   `pair<string, uint64_t>` the values are interleaved, so the same line holds ~2 keys. You
   halve your in-node search throughput for nothing. (This is the same "one file per
   concern / separate by temperature" argument from frontier doc 02 §1, applied at cache-line
   scale.)

> **Should `prev` exist too?** A doubly-linked leaf chain enables backward range scans and
> makes merge slightly easier. It costs 8 bytes per leaf and one more pointer to maintain
> correctly in split *and* merge — which is two more places to introduce a bug. **Verdict:
> skip it in v1.** Add it only when you have a concrete backward-scan requirement, and add
> it *after* the validator is checking the forward chain.

---

## 4. Separator key semantics — the concept that makes B+Trees click

Say this out loud until it's automatic:

> **An internal key is a *routing decision*, not a *record*.**

Consequences, each of which will save you hours:

- A separator may be a key that **was deleted**, or a key that **never existed**. Both are
  fine. It still routes correctly, because the only thing required of separator `kᵢ` is:
  everything in subtree `cᵢ₋₁` is `< kᵢ`, everything in `cᵢ..` is `≥ kᵢ`.
- Therefore **delete does not need to hunt down and repair separators** that happen to equal
  the deleted key. This is the single biggest simplification B+Trees have over B-Trees, and
  the thing most tutorials get wrong by over-repairing. Doc 05 §6.
- Therefore a separator can be **truncated**. If the left subtree's max key is `"laptop"`
  and the right subtree's min is `"lemon"`, any string in `("laptop", "lemon"]` is a valid
  separator — so you can store `"le"` instead of `"lemon"`. This is **suffix truncation**,
  and for a term dictionary it can double or triple your fanout. Doc 07 §7.
- Therefore **the same key appears twice** in the tree: once as data in a leaf, once (maybe)
  as a separator upstairs. Searching must not stop at the internal match — it must descend
  to the leaf. If your `search()` can return from an internal node, you built a B-Tree.

### Where separators come from

Two different rules, and confusing them is *the* classic insert bug (doc 04 §4):

| Split of a… | Separator sent to parent | Is it removed from the child? |
|---|---|---|
| **Leaf** | `rightLeaf.keys[0]` | **No — copied up.** The key is real data; it must remain in the leaf. |
| **Internal** | `node.keys[mid]` | **Yes — pushed up.** It's only a fence; it moves to become a fence one level higher. |

**Copy-up for leaves, push-up for internals.** Write that on a sticky note.

Why the asymmetry, from the semantics above: leaf keys are *records* — deleting one would
lose data. Internal keys are *fences* — a fence moved upward still fences the same boundary,
and leaving a copy behind would create a fence with only one side.

---

## 5. The eight invariants (I1–I8)

These define "a valid B+Tree." Every public operation must leave all eight true. The
validator in §6 checks each one; the numbering is used throughout docs 04, 05, and 09.

**I1 — Ordering within a node.**
`keys` is strictly ascending: `keys[0] < keys[1] < ... < keys[n-1]`. Strictly — no duplicates
inside a node. (Duplicate *keys in the tree* is a separate policy question, §7.)

**I2 — Child count.**
For an internal node: `children.size() == keys.size() + 1`. For a leaf:
`children.empty() && values.size() == keys.size()`.

**I3 — Leaf entry pairing.**
In a leaf, `keys.size() == values.size()`, and entry `i` is the pair `(keys[i], values[i])`.

**I4 — Occupancy.**
- Non-root internal: `INTERNAL_MIN_CHILDREN ≤ children.size() ≤ INTERNAL_MAX_CHILDREN`
- Non-root leaf: `LEAF_MIN_ENTRIES ≤ keys.size() ≤ LEAF_MAX_ENTRIES`
- **Root, if internal:** `2 ≤ children.size() ≤ INTERNAL_MAX_CHILDREN` (a 1-child root is
  illegal — it must be collapsed; doc 05 §5)
- **Root, if leaf:** `0 ≤ keys.size() ≤ LEAF_MAX_ENTRIES` (an empty tree is one empty leaf)

**I5 — Subtree range containment.**
For internal node with keys `k₀..k_{n-1}` and children `c₀..c_n`, every key `x` in the
subtree rooted at `cᵢ` satisfies `k_{i-1} ≤ x < kᵢ` (with `k₋₁ = -∞`, `k_n = +∞`).
*This is the deep one.* I1 and I2 are local and cheap; I5 is global and is what actually
guarantees search correctness. Check it by passing `(lo, hi)` bounds down the recursion.

**I6 — Uniform leaf depth.**
Every leaf is at exactly the same distance from the root. Check by collecting the depth of
every leaf and asserting the set has size 1.

**I7 — Leaf chain integrity.**
Following `next` from the leftmost leaf visits **every** leaf exactly once, in left-to-right
order, and the concatenation of all their keys is strictly ascending. The last leaf's `next`
is `nullptr`.

**I8 — Separator provenance.**
For each internal key `kᵢ`: `kᵢ` is `>` every key in subtree `cᵢ₋₁` and `≤` the minimum key
in subtree `cᵢ`. (Strictly this is implied by I5, but checking it *directly* — by computing
each subtree's actual min/max — catches split bugs that I5's bound-passing can mask when the
bounds themselves were computed from the broken keys.)

> **Which ones catch which bug** — this table is the reason to write the validator first:
>
> | Broken invariant | Almost certainly a bug in… |
> |---|---|
> | I2 | Split: you moved keys and children with mismatched index ranges (doc 04 §4) |
> | I4 (too few) | Delete: underflow not handled, or borrow/merge threshold wrong (doc 05 §3) |
> | I4 (too many) | Insert: you didn't split, or split at the wrong threshold (doc 04 §2) |
> | I5/I8 | Split: wrong separator chosen, or copy-up/push-up confused (doc 02 §4) |
> | I6 | Split: you created a node at the wrong level, or forgot the root split (doc 04 §5) |
> | I7 | Split: forgot `newLeaf->next = leaf->next; leaf->next = newLeaf;` — or merge: forgot to relink (doc 04 §3, doc 05 §4) |

---

## 6. The validator — build this first

Not optional. This is your debugger for the next six docs.

```cpp
#include <cassert>
#include <limits>
#include <optional>
#include <vector>

// Returns the depth of every leaf found, and asserts I1..I8 along the way.
// lo / hi are the exclusive-lower / exclusive-upper bounds this subtree must respect (I5).
// Pass std::nullopt for -inf / +inf.
struct ValidationResult {
    int      leafDepth;   // depth at which leaves were found in this subtree
    Key      minKey;      // actual minimum key in this subtree (for I8)
    Key      maxKey;      // actual maximum key in this subtree (for I8)
};

ValidationResult validateSubtree(Node* node, int depth, bool isRoot,
                                 std::optional<Key> lo, std::optional<Key> hi)
{
    assert(node != nullptr);

    // ---- I1: keys strictly ascending -------------------------------------
    for (size_t i = 1; i < node->keys.size(); ++i)
        assert(node->keys[i - 1] < node->keys[i] && "I1: keys not strictly ascending");

    // ---- I5: every key inside the inherited bounds ------------------------
    for (const Key& k : node->keys) {
        if (lo) assert(!(k < *lo) && "I5: key below subtree lower bound");
        if (hi) assert(k < *hi   && "I5: key at/above subtree upper bound");
    }

    if (node->isLeaf) {
        // ---- I2 / I3 -------------------------------------------------------
        assert(node->children.empty()                  && "I2: leaf has children");
        assert(node->values.size() == node->keys.size() && "I3: key/value size mismatch");

        // ---- I4: occupancy (root exempt) -----------------------------------
        assert((int)node->keys.size() <= LEAF_MAX_ENTRIES && "I4: leaf overfull");
        if (!isRoot)
            assert((int)node->keys.size() >= LEAF_MIN_ENTRIES && "I4: leaf underfull");

        assert(!node->keys.empty() || isRoot);  // only the root leaf may be empty
        return { depth, node->keys.front(), node->keys.back() };
    }

    // ---- I2: fencepost relationship --------------------------------------
    assert(node->children.size() == node->keys.size() + 1 && "I2: children != keys+1");
    assert(node->values.empty()  && "I2: internal node carries values (that's a B-Tree)");

    // ---- I4: occupancy ----------------------------------------------------
    assert((int)node->children.size() <= INTERNAL_MAX_CHILDREN && "I4: internal overfull");
    if (isRoot) assert(node->children.size() >= 2 && "I4: root has < 2 children — collapse it");
    else        assert((int)node->children.size() >= INTERNAL_MIN_CHILDREN && "I4: internal underfull");

    // ---- recurse, tightening bounds per child (I5) ------------------------
    std::optional<int> commonDepth;
    Key subtreeMin{}, subtreeMax{};

    for (size_t i = 0; i < node->children.size(); ++i) {
        std::optional<Key> childLo = (i == 0) ? lo : std::optional<Key>(node->keys[i - 1]);
        std::optional<Key> childHi = (i == node->keys.size()) ? hi
                                                             : std::optional<Key>(node->keys[i]);

        ValidationResult r = validateSubtree(node->children[i], depth + 1, false,
                                             childLo, childHi);

        // ---- I6: all leaves at the same depth ----------------------------
        if (!commonDepth) commonDepth = r.leafDepth;
        else assert(*commonDepth == r.leafDepth && "I6: leaves at differing depths");

        // ---- I8: separator is a true boundary of the ACTUAL subtree ranges -
        if (i > 0) {
            assert(node->keys[i - 1] <= r.minKey && "I8: separator > right subtree min");
            // and the left neighbour's max must be strictly below it:
            assert(subtreeMax < node->keys[i - 1] && "I8: left subtree max >= separator");
        }

        if (i == 0) subtreeMin = r.minKey;
        subtreeMax = r.maxKey;
    }

    return { *commonDepth, subtreeMin, subtreeMax };
}

// ---- I7: the leaf chain, checked separately from the top -----------------
void validateLeafChain(Node* root) {
    Node* leaf = root;
    while (!leaf->isLeaf) leaf = leaf->children.front();   // leftmost leaf

    std::optional<Key> prev;
    size_t leavesSeen = 0;
    while (leaf) {
        for (const Key& k : leaf->keys) {
            if (prev) assert(*prev < k && "I7: leaf chain not globally ascending");
            prev = k;
        }
        ++leavesSeen;
        assert(leavesSeen < 1'000'000 && "I7: leaf chain appears to be a cycle");
        leaf = leaf->next;
    }
    // Cross-check against a count of leaves reached via children pointers:
    assert(leavesSeen == countLeavesViaChildren(root) && "I7: chain skips or duplicates a leaf");
}

void validate(Node* root) {
    if (!root) return;
    validateSubtree(root, 0, /*isRoot=*/true, std::nullopt, std::nullopt);
    validateLeafChain(root);
}
```

**How to use it:** in your test build, call `validate(root)` after **every single**
`insert()` and `remove()`. Not at the end of the test — after every operation. When it
fires, the assert message names the invariant, the invariant table in §5 names the likely
routine, and you're looking at ~30 lines of suspect code. This turns B+Tree debugging from
"stare at a segfault" into a lookup.

Guard it so it doesn't ship in release builds — the cost is O(N) per operation, which is
fine for a 10,000-key test and catastrophic in production:

```cpp
#ifdef BPTREE_VALIDATE
  #define BPTREE_CHECK(root) validate(root)
#else
  #define BPTREE_CHECK(root) ((void)0)
#endif
```

> **Also write `printTree()` now.** A level-order dump like the diagrams in this doc. You
> will read it hundreds of times. Doc 09 §2 gives a version.

---

## 7. Policy decisions you must make before writing code

These aren't invariants — they're choices. Make them explicitly and write the choice into a
comment at the top of your header, because ambiguity here produces inconsistent behaviour
across `insert`/`search`/`remove` that is *very* hard to spot.

### 7.1 Duplicate keys

| Policy | Behaviour | Use when |
|---|---|---|
| **Unique keys** *(recommended)* | `insert(k, v)` on existing `k` **overwrites** `v` and returns `false` | Term dictionary: one term, one posting-list offset |
| Multi-value | Value is a container; insert appends | Term → multiple docs, if you don't have a separate postings file |
| True duplicates | Multiple entries with equal keys, spanning leaves | Almost never worth it — breaks I1, complicates search enormously |

**Pick unique keys.** If you need multiplicity, make `Value` a `std::vector<T>` — the tree
stays simple and the complexity lives in a type you already understand. True duplicate keys
in a B+Tree require the search to scan *backwards* across leaf boundaries to find the first
match, and every equality comparison in routing becomes ambiguous. Real databases that
support it (non-unique secondary indexes) do so by appending the row-id to the key, making
it unique again. Do that if you need it.

### 7.2 Deletion strategy

| Strategy | Cost | Notes |
|---|---|---|
| **Full rebalance** | Complex (doc 05) | Textbook-correct; guarantees I4 |
| **Lazy / tombstone** | Trivial | Remove from leaf, allow underflow, never merge |

Lazy delete is **not cheating** — frontier doc 03 tells you Lucene deletes are tombstones,
and many production B+Trees (including some in real DBs) skip merging entirely, relying on
periodic rebuild instead. The tree stays *correct* (I1, I2, I3, I5, I6, I7 all hold), it
just degrades occupancy (I4 relaxed). **Build lazy first** — you get a working tree faster —
**then implement full rebalance in doc 05** and keep both behind a flag, so you can A/B them.

### 7.3 Parent pointers vs a path stack

To split a node you must insert a separator into its **parent**. How do you get there?

- **Parent pointer in every node:** `node->parent`. Easy to follow, but every split, merge,
  and root change must fix up parent pointers on potentially *many* children — a rich source
  of bugs — and it makes the node non-serializable (a parent pointer is meaningless on disk,
  and a page can't cheaply know its parent).
- **Path stack** *(recommended)*: the descent already visits root→leaf. Record it.
  `std::vector<Node*> path` (or `vector<pair<Node*, int>>` to also remember which child index
  you took). Unwind it to propagate splits upward.

**Use the path stack.** It costs `h` pointers (~4) on the stack, needs no maintenance, works
identically for the disk version in doc 08 (where you'd stack page-ids), and makes it
obvious that split propagation is a *bottom-up unwind* rather than magic. It's also the
natural fit for the recursive formulation in doc 04 §6, where the call stack *is* the path
stack.

---

## 8. Checkpoint before doc 03

1. Why does a leaf have `keys.size() == values.size()` but an internal node have
   `children.size() == keys.size() + 1`? Answer with the fencepost picture. (§2)
2. State I5 from memory. Why is it "the deep one"? (§5)
3. A separator key equals a key you just deleted from a leaf. Do you need to update the
   separator? Why or why not? (§4)
4. Which invariant does the leaf-chain check cover, and which operation most often breaks
   it? (§5)
5. Why does a vector-based node take *two* cache misses per level instead of one? (§1)
6. You choose parent pointers over a path stack. Name two things that get harder. (§7.3)

**Do this now, before doc 03:** create `internal/kernal/core/datastructures/bplustree.hpp`
with (a) the four capacity constants from doc 01 §5, (b) the `Node` struct from §1 Option A,
(c) the validator from §6, (d) a `printTree()`. Hand-build a 2-level tree in a test by
`new`-ing nodes and wiring them yourself — no `insert()` yet — and confirm `validate()`
passes. Then deliberately break each invariant one at a time (delete a child pointer,
swap two keys, unlink a leaf) and confirm the validator catches each. **A validator you
haven't tested against known-bad trees is not a validator.**
