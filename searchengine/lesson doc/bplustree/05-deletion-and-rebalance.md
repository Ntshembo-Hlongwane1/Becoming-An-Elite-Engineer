# 05 — Deletion & Rebalancing

> **The hardest doc.** Insert has one repair operation (split). Delete has three (borrow
> left, borrow right, merge), each with a leaf variant and an internal variant, each needing
> a different separator fix-up. That's six cases plus root collapse.
>
> **Read §1 first and take the escape hatch seriously.** Lazy deletion is a legitimate
> production choice, it's 10 lines, and it lets you ship a working tree today. Then come back
> and do the full version — with the validator on, and with the case table in §8 next to you.

---

## 1. First: do you actually need rebalancing?

Deletion without rebalancing — remove the entry from its leaf, allow the node to go below
`LEAF_MIN_ENTRIES`, never merge — leaves the tree **correct**:

- I1 (ordering) ✓ — removing an element from a sorted array keeps it sorted
- I2 (fencepost) ✓ — leaves have no children; internal nodes are untouched
- I3 (pairing) ✓ — remove key and value at the same index
- I5 (containment) ✓ — removing keys can only *shrink* a subtree's range
- I6 (uniform depth) ✓ — no node is ever removed, so no level changes
- I7 (chain) ✓ — untouched
- I8 (separators) ✓ — a separator is a bound, not a record (doc 02 §4)
- **I4 (occupancy) ✗** — and this is the *only* one, and it costs only space

So the entire cost of lazy delete is **occupancy**. Search, scan, insert, and iteration all
remain fully correct. In the pathological case (delete 99% of keys) you get a tree of nearly
empty leaves — height stays right, but you touch far more nodes per scan than necessary.

| | Lazy delete | Full rebalance |
|---|---|---|
| Code | ~10 lines | ~200 lines, 6 cases |
| Correctness | Full (I4 relaxed) | Full |
| Space after heavy deletion | Degrades badly | Bounded ≥ 50% |
| Scan cost after heavy deletion | Degrades | Bounded |
| Used in production | **Yes** — Lucene tombstones, many embedded B+Trees | Yes — InnoDB, SQLite |

**Recommended path:** implement lazy delete *now*, get a fully working tree, write the test
suite against it, **then** implement full rebalance behind a compile-time flag and re-run the
same suite. The suite is the safety net that makes the hard version tractable.

```cpp
// Lazy delete: remove from the leaf, tolerate underflow. Deliberately does not rebalance.
bool removeLazy(const Key& key) {
    if (!root) return false;
    Node* leaf = descendToLeaf(key).leaf;
    int pos = lowerBoundIdx(leaf->keys, key, less);
    if (pos >= (int)leaf->keys.size() || !eq(leaf->keys[pos], key)) return false;

    leaf->keys.erase  (leaf->keys.begin()   + pos);
    leaf->values.erase(leaf->values.begin() + pos);
    --entryCount;
    return true;
}
```

Note what is **not** here: no separator repair. Deleting the key `30` when `30` is also a
separator upstairs requires **no action at all**. §6 explains why in full — it's the fact
that makes B+Tree deletion simpler than B-Tree deletion, and the fact people most often
disbelieve.

**If you take the lazy path, also add a rebuild:** `if (entryCount < capacity/4) rebuild()`
using the bulk loader (doc 04 §8.2 / doc 07 §8). `O(N)` occasionally, amortised to nothing,
and it restores 100% occupancy. Frontier doc 03's segment merge is precisely this idea at
the index level — you already know the pattern.

---

## 2. Full deletion, top level

```
 remove(key):
   1. descend to the leaf, recording the path                (doc 03 §4)
   2. if key not in the leaf → return false
   3. erase (key, value) from the leaf
   4. if leaf is not underfull → done                        (the common case)
   5. repair the leaf: try borrow-from-left, borrow-from-right, else MERGE
   6. if the repair MERGED, the parent lost a key → it may now be underfull
        → walk up, repairing internal nodes the same way
   7. if the root ends up with 1 child → collapse it (height -1)
```

Structurally the mirror image of insert:

| | Insert | Delete |
|---|---|---|
| Trigger | Overfull (`> MAX`) | Underfull (`< MIN`) |
| Repair | Split (one option) | Borrow **or** merge (three options) |
| Propagates when | Always after a split | Only after a **merge** — a borrow is local |
| Terminal case | Root splits → height +1 | Root has 1 child → height −1 |

> **"Only after a merge."** A borrow moves one entry between siblings and adjusts one
> separator — the parent's *child count is unchanged*, so the parent cannot become underfull
> and the cascade **stops immediately**. Only a merge removes a child from the parent. That's
> why you always **prefer borrowing**: it's `O(1)` and terminal, where a merge can cascade to
> the root.

---

## 3. Underflow and the repair decision

```cpp
bool leafUnderfull(Node* n)     { return (int)n->keys.size()     < LEAF_MIN_ENTRIES; }
bool internalUnderfull(Node* n) { return (int)n->children.size() < INTERNAL_MIN_CHILDREN; }
// The ROOT is exempt from both (I4). Never call these on the root.
```

Given an underfull node at child index `i` of `parent`:

```
              ┌───────────────────────────────────┐
              │ can I borrow from the LEFT sibling?│   (i > 0 AND sibling has > MIN)
              └───────────┬──────────────┬────────┘
                        yes│              │no
                           ▼              ▼
                    borrow left    ┌───────────────────────────────────┐
                    (DONE — stop)  │ can I borrow from the RIGHT sibling│  (i < n AND > MIN)
                                   └────────┬──────────────┬───────────┘
                                          yes│              │no
                                             ▼              ▼
                                      borrow right      MERGE with a sibling
                                      (DONE — stop)     (parent loses a key →
                                                         recurse upward)
```

**Only ever use a sibling that shares the same parent.** The node "next to" yours in key
order might be under a different parent entirely (a *cousin*), and borrowing from a cousin
would require fixing separators at a higher level — an enormous complication for no benefit.
Every sibling reference in this doc means `parent->children[i ± 1]`.

**Why is a merge always possible when no borrow is?** If neither sibling can spare an entry,
both are at exactly `MIN`, and our node has `MIN - 1`. Their combined size is
`2·MIN - 1 ≤ MAX` — because `MIN = (MAX+1)/2`, so `2·MIN ≤ MAX+1`, so `2·MIN - 1 ≤ MAX` ✓.
**The merged node always fits.** That's the second place the ceiling in the constants earns
its keep (the first was doc 04 §3). If you ever redefine `MIN`, re-verify this inequality or
merges will overflow.

**Also: a non-root node always has at least one sibling**, because a non-root node's parent
has `≥ INTERNAL_MIN_CHILDREN ≥ 2` children. So the decision tree never falls through.

---

## 4. The six repair cases

Notation used throughout: the underfull node is `node = parent->children[i]`.
- Separator **left of** `node` (between `children[i-1]` and `children[i]`) is `parent->keys[i-1]`
- Separator **right of** `node` (between `children[i]` and `children[i+1]`) is `parent->keys[i]`

Keep that straight and the six cases are mechanical.

### 4.1 Leaf borrows from LEFT sibling

```
 BEFORE                                    AFTER
        parent: [... 50 ...]                      parent: [... 40 ...]   ← separator UPDATED
                 ↙       ↘                                 ↙       ↘
   left:[20,30,40]      node:[50]           left:[20,30]        node:[40,50]
        (3 > MIN=2)      (1 < MIN=2)             (2 == MIN)       (2 == MIN)
```

```cpp
void leafBorrowLeft(Node* parent, int i) {
    Node* node = parent->children[i];
    Node* left = parent->children[i - 1];

    node->keys.insert  (node->keys.begin(),   std::move(left->keys.back()));
    node->values.insert(node->values.begin(), std::move(left->values.back()));
    left->keys.pop_back();
    left->values.pop_back();

    parent->keys[i - 1] = node->keys.front();   // new separator = node's new smallest key
}
```

The separator must become `node->keys.front()` because the boundary between the two leaves
moved left by one entry. Forget this and I5/I8 break: the borrowed key `40` still routes to
the *left* subtree (`40 < 50`) but now physically lives in `node` — **it becomes unfindable.**

### 4.2 Leaf borrows from RIGHT sibling

```
 BEFORE                                    AFTER
        parent: [... 60 ...]                      parent: [... 70 ...]   ← separator UPDATED
                 ↙       ↘                                 ↙       ↘
     node:[50]        right:[60,70,80]        node:[50,60]      right:[70,80]
```

```cpp
void leafBorrowRight(Node* parent, int i) {
    Node* node  = parent->children[i];
    Node* right = parent->children[i + 1];

    node->keys.push_back  (std::move(right->keys.front()));
    node->values.push_back(std::move(right->values.front()));
    right->keys.erase  (right->keys.begin());
    right->values.erase(right->values.begin());

    parent->keys[i] = right->keys.front();      // new separator = right's new smallest key
}
```

### 4.3 Leaf MERGE

Always merge `node` with its **right** sibling when one exists; otherwise merge the **left**
sibling into `node`. Normalising to "always merge `children[j]` and `children[j+1]`" removes
half the cases:

```
 BEFORE                                    AFTER
        parent: [... 60 ...]                      parent: [... ]     ← separator DISCARDED
                 ↙       ↘                                  ↓
     node:[50]        right:[60,70]            merged:[50,60,70]
                                                (and parent lost one key AND one child)
```

```cpp
// Merges children[j] and children[j+1] into children[j]. Both must be leaves.
void leafMerge(Node* parent, int j) {
    Node* left  = parent->children[j];
    Node* right = parent->children[j + 1];

    left->keys.insert  (left->keys.end(),
                        std::make_move_iterator(right->keys.begin()),
                        std::make_move_iterator(right->keys.end()));
    left->values.insert(left->values.end(),
                        std::make_move_iterator(right->values.begin()),
                        std::make_move_iterator(right->values.end()));

    left->next = right->next;                   // ---- I7: unlink from the chain ----

    parent->keys.erase    (parent->keys.begin()     + j);        // separator is DISCARDED
    parent->children.erase(parent->children.begin() + j + 1);

    delete right;                               // ---- ownership: right is gone ----
}
```

**The separator is discarded, not pulled down.** Why: leaf keys are real records and the
separator was only a *copy* of `right->keys[0]` (doc 04 §3's copy-up). That original still
exists in the merged leaf. Pulling it down would create a **duplicate key**, breaking I1.

> This is the exact mirror of copy-up. Insert *copies* a leaf key upward; delete *discards*
> the copy. Symmetric, and the symmetry is the check: if your merge pulls the separator
> down for leaves, your split must have removed it from the leaf — which would have deleted
> data. Neither is right.

### 4.4 Internal borrows from LEFT sibling — the rotation

Now the fences move, and it's a **three-way rotation** through the parent, not a straight
transfer.

```
 BEFORE                                          AFTER
        parent: [ ... 50 ... ]                          parent: [ ... 30 ... ]
                 ↙          ↘                                    ↙          ↘
  left:[10,30]              node:[70]           left:[10]                  node:[50, 70]
   c₀  c₁  c₂                cₓ  cᵧ              c₀  c₁                     c₂  cₓ  cᵧ
   (3 children)             (2 children)         (2 children)              (3 children)

   rotation:  left's last key (30) ──▶ up into parent
              parent's separator (50) ──▶ down into node's front
              left's last CHILD (c₂) ──▶ moves with it, to node's front
```

```cpp
void internalBorrowLeft(Node* parent, int i) {
    Node* node = parent->children[i];
    Node* left = parent->children[i - 1];

    // parent's separator comes DOWN to be node's new first key
    node->keys.insert(node->keys.begin(), std::move(parent->keys[i - 1]));
    // left's rightmost child moves across with it
    node->children.insert(node->children.begin(), left->children.back());

    // left's last key goes UP to become the new separator
    parent->keys[i - 1] = std::move(left->keys.back());

    left->keys.pop_back();
    left->children.pop_back();
}
```

**Why the separator comes *down* here but was merely *overwritten* in the leaf case (§4.1):**
the incoming child `c₂` holds keys in the range `[30, 50)`. Inside `node`, that subtree needs
a fence on its right separating it from `cₓ` — and the correct fence is exactly `50`, the old
parent separator. The fence at the parent level then has to be re-established at the new
boundary, which is `30`. Fences are never created or destroyed in a rotation; they **move
down one level and up one level**, and the child pointer moves with them. Count them: before,
3 fences total (`10, 30` in left, `50` in parent, `—` in node) — after, 3 fences (`10` in
left, `30` in parent, `50` in node). Conserved. ✓

### 4.5 Internal borrows from RIGHT sibling

The mirror image:

```
 BEFORE                                          AFTER
        parent: [ ... 50 ... ]                          parent: [ ... 70 ... ]
                 ↙          ↘                                    ↙          ↘
    node:[30]              right:[70,90]        node:[30,50]              right:[90]
     c₀  c₁                 cₓ  cᵧ  c_z          c₀  c₁  cₓ                cᵧ  c_z
```

```cpp
void internalBorrowRight(Node* parent, int i) {
    Node* node  = parent->children[i];
    Node* right = parent->children[i + 1];

    node->keys.push_back(std::move(parent->keys[i]));       // separator comes DOWN
    node->children.push_back(right->children.front());      // right's first child moves across

    parent->keys[i] = std::move(right->keys.front());       // right's first key goes UP

    right->keys.erase(right->keys.begin());
    right->children.erase(right->children.begin());
}
```

### 4.6 Internal MERGE — pull the separator DOWN

```
 BEFORE                                          AFTER
        parent: [ ... 50 ... ]                          parent: [ ... ]
                 ↙          ↘                                    ↓
    node:[30]              right:[70]              merged: [30, 50, 70]
     c₀  c₁                 cₓ  cᵧ                    c₀  c₁  cₓ  cᵧ
     (2 children)          (2 children)               (4 children)  ✓ = keys+1
```

```cpp
// Merges children[j] and children[j+1] into children[j]. Both must be internal.
void internalMerge(Node* parent, int j) {
    Node* left  = parent->children[j];
    Node* right = parent->children[j + 1];

    // ---- the separator is PULLED DOWN, between the two halves' keys ----
    left->keys.push_back(std::move(parent->keys[j]));

    left->keys.insert    (left->keys.end(),
                          std::make_move_iterator(right->keys.begin()),
                          std::make_move_iterator(right->keys.end()));
    left->children.insert(left->children.end(),
                          std::make_move_iterator(right->children.begin()),
                          std::make_move_iterator(right->children.end()));

    parent->keys.erase    (parent->keys.begin()     + j);
    parent->children.erase(parent->children.begin() + j + 1);

    delete right;
}
```

**Discarding the separator here would break I2.** Count: left has `a` keys / `a+1` children;
right has `b` keys / `b+1` children. Merged children `= a+b+2`, so merged keys must be
`a+b+1` — one more than `a+b`. The separator supplies exactly that one. It's also
*semantically* required: it's the only fence that separates `left`'s last child from
`right`'s first child, and nothing else in the tree records that boundary. **Discard it and
you lose the boundary and I5 breaks silently** — keys become unfindable, no crash.

> **Leaf merge discards, internal merge pulls down.** Same table as copy-up/push-up in doc 04
> §4, and for the same underlying reason: leaf keys are records (already present below);
> internal keys are fences (present nowhere else).

---

## 5. Root collapse — the only way a B+Tree gets shorter

After a merge at the level directly below the root, the root may be left with a **single
child**. That's illegal by I4 (a root must have ≥ 2 children) and pointless (a 1-way branch).

```
 BEFORE                        AFTER
    ┌──────┐
    │ root │  1 child          ┌──────────┐
    └───┬──┘                   │ 30    50 │  ← the old child IS the new root
        │                      └──────────┘
   ┌──────────┐                   height − 1
   │ 30    50 │
   └──────────┘
```

```cpp
void collapseRootIfNeeded() {
    if (!root || root->isLeaf) return;              // a leaf root is fine at any size, even 0
    if (root->children.size() != 1) return;

    Node* onlyChild = root->children.front();
    delete root;                                    // its single child pointer is now owned above
    root = onlyChild;
    --height;
}
```

Three things:

1. **This is the exact inverse of `growNewRoot`** (doc 04 §5), and like it, it moves *every*
   leaf up one level simultaneously — preserving I6 in `O(1)`.
2. **A leaf root is exempt entirely.** When the tree shrinks to a single leaf, that leaf is
   the root and may hold 0 entries. `root->isLeaf` guard first — without it you'd read
   `children` on a leaf.
3. Call it **once, at the end of `remove`**, after the upward cascade finishes. Calling it
   mid-cascade means the node you're about to repair may no longer be the root's child.

**Should an empty tree keep an empty leaf, or set `root = nullptr`?** Either, but be
consistent: `search`, `scan`, `begin()`, and the validator all need the same answer. Keeping
one empty leaf is simpler (fewer null checks downstream); `nullptr` frees the memory. Pick
one and write it in a comment.

---

## 6. The separator you do NOT need to fix

Here is the fact people refuse to believe. Delete `50` from this tree:

```
 BEFORE                                   AFTER
          ┌──────┐                             ┌──────┐
          │  50  │                             │  50  │   ← STILL 50. Untouched. Correct.
          └─┬──┬─┘                             └─┬──┬─┘
       ┌────┘  └────┐                     ┌──────┘  └────┐
   ┌────────┐  ┌────────────┐        ┌────────┐  ┌────────────┐
   │ 10  30 │  │ 50  70  90 │        │ 10  30 │  │   70   90  │
   └────────┘  └────────────┘        └────────┘  └────────────┘
```

The separator `50` is now a key that **exists nowhere in the tree**. Ask whether it still
routes correctly:

- `search(30)`: `30 < 50` → left → found ✓
- `search(70)`: `70 ≥ 50` → right → found ✓
- `search(50)`: `50 ≥ 50` → right → not in leaf → `nullptr` ✓ **correct, it was deleted**
- `insert(50)`: routes right, lands in `[70,90]` → `[50,70,90]` ✓ still sorted, still
  correctly bounded by the separator

**Every operation is correct.** I5 requires only that everything left of the fence is `< 50`
and everything right is `≥ 50`. Both still hold. The fence doesn't have to stand on a
record; it only has to stand in the right *place*.

Contrast a **B-Tree**, where `50` in the internal node was the actual record. Deleting it
means deleting from an internal node, which means finding the in-order predecessor or
successor in a leaf, promoting it up, and *then* handling the underflow that promotion caused
in that leaf. That's the extra machinery B+Trees don't need, and it's the concrete payoff of
"data only in leaves."

> **Corollary: never write code that searches internal nodes for a deleted key.** If you
> catch yourself writing "find and update every separator equal to `key`", stop — it's
> `O(h)` wasted work at best, and at worst you'll write a "fix" that violates I5.
>
> **The one time a separator *is* updated is a borrow** (§4.1, §4.2) — and even then it's not
> because a key was deleted; it's because the physical **boundary between two nodes moved**.
> That's a different trigger entirely. Keep them separate in your head.

---

## 7. The full `remove` — recursive

```cpp
// Removes `key` from the subtree rooted at `node`.
// Returns true if a key was removed. The CALLER repairs `node` if it underflows.
bool removeFrom(Node* node, const Key& key) {
    if (node->isLeaf) {
        int pos = lowerBoundIdx(node->keys, key, less);
        if (pos >= (int)node->keys.size() || !eq(node->keys[pos], key)) return false;
        node->keys.erase  (node->keys.begin()   + pos);
        node->values.erase(node->values.begin() + pos);
        return true;
    }

    int i = upperBoundIdx(node->keys, key, less);
    if (!removeFrom(node->children[i], key)) return false;

    // ---- the child may now be underfull; repair it from HERE (we are its parent) ----
    Node* child = node->children[i];
    bool underfull = child->isLeaf ? leafUnderfull(child) : internalUnderfull(child);
    if (underfull) repairChild(node, i);

    return true;
}

// Restores I4 for parent->children[i]. May remove a child from `parent`.
void repairChild(Node* parent, int i) {
    Node* node    = parent->children[i];
    const bool lf = node->isLeaf;
    const int  minSize = lf ? LEAF_MIN_ENTRIES : INTERNAL_MIN_CHILDREN;
    auto sizeOf = [&](Node* n) { return lf ? (int)n->keys.size() : (int)n->children.size(); };

    // ---- 1. borrow from LEFT (preferred: O(1), no cascade) ----------------
    if (i > 0 && sizeOf(parent->children[i - 1]) > minSize) {
        lf ? leafBorrowLeft(parent, i) : internalBorrowLeft(parent, i);
        return;
    }
    // ---- 2. borrow from RIGHT ---------------------------------------------
    if (i + 1 < (int)parent->children.size() && sizeOf(parent->children[i + 1]) > minSize) {
        lf ? leafBorrowRight(parent, i) : internalBorrowRight(parent, i);
        return;
    }
    // ---- 3. MERGE. Normalise to "merge children[j] and children[j+1]". ----
    int j = (i > 0) ? i - 1 : i;      // if we have a left sibling, WE are the right half
    lf ? leafMerge(parent, j) : internalMerge(parent, j);
    // `parent` lost a key and a child; ITS parent will repair it on the way up.
}

bool remove(const Key& key) {
    if (!root) return false;
    if (!removeFrom(root, key)) return false;
    --entryCount;
    collapseRootIfNeeded();           // once, at the very end (§5)
    return true;
}
```

### Three details worth stopping on

**(a) The parent repairs the child, never the child itself.** `removeFrom` returns to the
parent frame, and *that* frame checks and repairs. This is deliberate: repair needs the
parent (for the separator and the siblings), and the recursion naturally hands you a frame
where you're standing at the parent with `i` in hand. This is why there's no path stack here
— the call stack is it.

**(b) The root is never repaired, only collapsed.** `removeFrom(root, ...)` runs the repair
for root's *children*, but nothing repairs root itself — correct, because I4 exempts it. The
only root-level action is `collapseRootIfNeeded`.

**(c) `int j = (i > 0) ? i - 1 : i;`** — the normalisation. If `node` has a left sibling,
merge `(i-1, i)` so `node` is the right half and gets absorbed. Otherwise `node` is
`children[0]` and must absorb its right sibling: merge `(i, i+1)`. Either way `leafMerge` /
`internalMerge` only ever handle "merge `j` into `j+1`" — halving the case count. Getting
this backwards produces an out-of-range access on the leftmost or rightmost child.

---

## 8. The case table

The reference card. When a delete test fails, find the case and re-read its section.

| # | Node kind | Repair | Separator action | Cascades? | § |
|---|---|---|---|---|---|
| 1 | Leaf | Borrow left | `parent->keys[i-1] = node->keys.front()` | No | 4.1 |
| 2 | Leaf | Borrow right | `parent->keys[i] = right->keys.front()` | No | 4.2 |
| 3 | Leaf | Merge | **Discarded** (was a copy) | Yes | 4.3 |
| 4 | Internal | Borrow left | Rotate: sep ↓ into node, left's last key ↑ | No | 4.4 |
| 5 | Internal | Borrow right | Rotate: sep ↓ into node, right's first key ↑ | No | 4.5 |
| 6 | Internal | Merge | **Pulled down** between the halves | Yes | 4.6 |
| 7 | Root (internal) | 1 child → collapse | n/a | Height −1 | 5 |

Compressed to three rules:

- **Borrow ⇒ no cascade. Merge ⇒ cascade.**
- **Leaf merge discards the separator; internal merge pulls it down.**
- **Leaf borrow overwrites the separator; internal borrow rotates through it, and the child
  pointer moves with the key.**

---

## 9. Worked trace — deleting from doc 04's tree

Starting from the end state of doc 04 §7 (`INTERNAL_MAX_CHILDREN = 4`, `LEAF_MAX_ENTRIES = 4`,
so `INTERNAL_MIN_CHILDREN = 2`, `LEAF_MIN_ENTRIES = 2`):

```
                          ┌────────┐
                          │   70   │
                          └─┬────┬─┘
              ┌─────────────┘    └──────────────┐
        ┌───────────┐                      ┌────────┐
        │  30   50  │                      │   90   │
        └─┬───┬───┬─┘                      └─┬────┬─┘
     ┌────┘   │   └────┐              ┌──────┘    └──────┐
   A[10,20] B[30,40] C[50,60]      D[70,80]      E[90,100,110]
```

**Delete 10.** → `A = [20]`, size 1 < MIN=2 → underfull. `A` is `children[0]` of `[30,50]`,
no left sibling. Right sibling `B=[30,40]` has size 2, **not > MIN** — can't borrow.
→ **merge**, `j = 0`: merge `A` and `B`.

```
   A becomes [20, 30, 40];  A->next = B->next = C;  parent discards separator 30.
   parent [30,50] → [50], children [A, C] → 2 children == INTERNAL_MIN ✓ not underfull. STOP.

                          ┌────────┐
                          │   70   │
                          └─┬────┬─┘
              ┌─────────────┘    └──────────────┐
        ┌──────┐                           ┌────────┐
        │  50  │                           │   90   │
        └─┬──┬─┘                           └─┬────┬─┘
     ┌────┘  └────┐                   ┌──────┘    └──────┐
  A[20,30,40]  C[50,60]            D[70,80]      E[90,100,110]
```

**Delete 20.** → `A = [30,40]`, size 2 == MIN. Not underfull. **Done — no repair.**

**Delete 30.** → `A = [40]`, size 1 → underfull. No left sibling. Right sibling `C=[50,60]`
size 2, not > MIN. → **merge** `j=0`: `A = [40,50,60]`, parent `[50]` → `[]` with 1 child.

```
   parent (the [50] node) now has 0 keys, 1 child → 1 < INTERNAL_MIN=2 → UNDERFULL.
   Cascade up: this node is children[0] of the root [70].
     - no left sibling
     - right sibling is [90] with 2 children, not > MIN=2 → can't borrow
   → INTERNAL MERGE, j=0: pull the root's separator 70 DOWN.

   merged: keys = [] + [70] + [90] = [70, 90];  children = [A] + [D, E] = [A, D, E]
           3 children == 2 keys + 1 ✓ I2

   root now has 0 keys and 1 child → COLLAPSE (§5), height 3 → 2.
```

Final:

```
                    ┌────────────┐
                    │  70    90  │
                    └─┬───┬────┬─┘
             ┌────────┘   │    └────────┐
       A[40,50,60]   D[70,80]    E[90,100,110]
```

Check the invariants: I2 — 2 keys/3 children ✓. I4 — root exempt, all leaves ≥ 2 ✓. I5 —
`70` and `90` fence correctly ✓. I6 — all leaves at depth 1 ✓. I7 — `40,50,60,70,80,90,100,110`
ascending and complete ✓. I8 — `70` = min of D ✓, `90` = min of E ✓.

**Note the separator `50` vanished entirely and `70` moved down a level.** Both are fences
being rearranged, and no record was harmed. That's §6's point made concrete.

---

## 10. Failure modes — symptom → cause

| Symptom | Likely cause | § |
|---|---|---|
| Validator **I4 (underfull)** persists after delete | Underflow check uses `<=` instead of `<`, or you check the node instead of the child | §3, §7 |
| Validator **I2** after an internal merge | Separator discarded instead of pulled down — merged node has `keys+2` children | §4.6 |
| Validator **I1** (duplicate key) after a leaf merge | Separator pulled down instead of discarded | §4.3 |
| Keys **unfindable** after a borrow, tree otherwise valid | Separator not updated (§4.1/4.2) — the moved key still routes to its old node | §4.1 |
| **Crash / out-of-range** merging the leftmost or rightmost child | `j` normalisation missing or inverted | §7(c) |
| Tree height never decreases; root has 1 child | `collapseRootIfNeeded` not called, or called before the cascade finished | §5 |
| `scan()` **skips a leaf** after deletes | `leafMerge` forgot `left->next = right->next` | §4.3 |
| **Use-after-free / double-free** | `delete right` before its keys/values were moved out, or the parent still holds the pointer | §4.3, §4.6 |
| Height decreases but leaves are at mixed depths | Collapse called mid-cascade on a non-root node | §5 |
| Borrow taken from a **cousin** (different parent) | Sibling looked up by leaf-chain `next` instead of `parent->children[i±1]` | §3 |

---

## 11. Checkpoint before doc 06

1. Why is lazy delete *correct*? Which single invariant does it relax, and what does that
   cost? (§1)
2. Why does a borrow never cascade but a merge always might? (§2)
3. Prove that when neither sibling can lend, the merged node still fits within `MAX`. (§3)
4. Leaf merge **discards** the separator; internal merge **pulls it down**. Justify both from
   what internal vs leaf keys *mean*. (§4.3, §4.6)
5. You delete a key that's also a separator. What must you do? Why? (§6)
6. Why does the *parent* repair the child rather than the child repairing itself? (§7a)
7. Explain `int j = (i > 0) ? i - 1 : i;` and what breaks without it. (§7c)

**Build now, in this order:**
1. `removeLazy` + tests. Confirm the tree still validates against I1–I3, I5–I8.
2. The six repair functions, each with a **hand-built** unit test that constructs exactly the
   before-state of the diagram in §4.x, calls the one function, and asserts the after-state.
   Test them in isolation before wiring them into `remove` — otherwise a bug in case 4 looks
   identical to a bug in case 6.
3. `repairChild` + `remove` + `collapseRootIfNeeded`.
4. The killer test: insert `1..1000`, then delete all 1000 **in random order**, calling
   `validate()` after every delete, and asserting the tree ends as a single empty leaf. Then
   the same with a 50/50 random mix of inserts and deletes over 100,000 operations, checked
   against a `std::map` oracle (doc 09 §5).

When the random insert/delete mix runs 100,000 operations with per-operation validation and
matches the `std::map` oracle exactly, **your B+Tree is correct.** Everything from doc 06
onward is about making it fast, safe, and persistent — not about making it work.
