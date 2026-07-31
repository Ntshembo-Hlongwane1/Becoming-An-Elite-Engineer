# 04 — Insertion & Splits

> **This is the doc.** Everything before it was setup. Insertion is where the tree's
> self-balancing lives, and it is ~80 lines of code with four places to make an off-by-one.
> Read it with paper next to you and **trace §7 by hand before reading §7's answer.**
>
> The core idea in one sentence: *insert into the leaf; if a node overflows, cut it in half
> and hand a separator to the parent; if the parent overflows, repeat; if the root
> overflows, grow a new root.*

---

## 1. The algorithm, top level

```
 insert(key, value):
   1. descend to the leaf that should hold key       (doc 03 §4 — same routing)
   2. if key already present → overwrite value, done (unique-key policy, doc 02 §7.1)
   3. insert (key, value) into the leaf, in sorted position
   4. if leaf is not overfull → done
   5. split the leaf → produces (newRightNode, separatorKey)
   6. walk back UP the path:
        insert (separatorKey, newRightNode) into the parent
        if parent not overfull → done
        split the parent → produces a new (newRightNode, separatorKey)
        repeat
   7. if we ran out of parents (the ROOT split) → make a new root with 2 children
```

Two structural observations before any code:

- **Steps 5–6 are a loop, and it usually runs zero times.** With fanout `f`, roughly `1/f` of
  inserts cause a leaf split, `1/f²` cause a parent split, and so on. At `f = 170`, one insert
  in 170 splits a leaf, and one in 28,900 splits its parent. **Insertion is amortised O(1)
  structural work** on top of the `O(log_f N)` descent. This is why B+Trees have good write
  throughput despite the scary-sounding cascade.
- **Everything propagates upward and nothing propagates downward.** A split never touches
  a node's children (their pointers are just *partitioned* between two parents), and never
  touches a sibling. The blast radius of an insert is one root-to-leaf path. That's what
  makes the recursion in §6 clean and what makes concurrent B+Trees possible at all.

---

## 2. Overflow: insert first, then check

There are two schools. Know both; use the first.

### School A — insert, then split if overfull *(use this)*

Allow the node to hold `MAX + 1` entries **transiently**, then split.

- **Pro:** the split logic is written once, and the "what do I split" question has one
  answer: "this node, which has exactly `MAX+1` entries."
- **Pro:** you only split nodes that genuinely overflowed. School B splits nodes that
  turn out not to have needed it, wasting space and lowering occupancy.
- **Con:** with fixed-size arrays (doc 02 §1 Option D) you must size them `MAX + 1` to hold
  the transient state — or split into a scratch buffer. A tiny cost.

### School B — preemptive split on the way down

While descending, split any *full* node you pass through. Then a leaf insert can never
cascade, because every ancestor is guaranteed to have room.

- **Pro:** single top-down pass, no unwind, no path stack — which makes **latch-crabbing**
  concurrency much simpler. This is why real concurrent B+Trees often use it.
- **Con:** splits nodes unnecessarily; average occupancy drops; more total splits.

**Verdict: School A.** It's easier to get right and easier to reason about, and you're not
writing a concurrent tree yet. Revisit School B only if doc 07's benchmarks push you toward
fine-grained locking.

So the check is:

```cpp
bool leafOverfull(Node* n)     { return (int)n->keys.size()     >  LEAF_MAX_ENTRIES; }
bool internalOverfull(Node* n) { return (int)n->children.size() >  INTERNAL_MAX_CHILDREN; }
```

> **Note the `>` not `>=`.** A leaf holding exactly `LEAF_MAX_ENTRIES` is **legal and full**,
> not overfull. Using `>=` splits one entry early — which isn't *wrong* (the tree stays
> valid) but it silently lowers your occupancy and, more annoyingly, makes every hand-trace
> in every textbook disagree with your output. Debugging is much easier when your tree
> matches the reference behaviour.

---

## 3. Splitting a leaf — copy-up

```
 LEAF_MAX_ENTRIES = 4.  Leaf is full: [10, 20, 30, 40].  Insert 25.

 step 1: insert in sorted position → transiently 5 entries
   ┌──────────────────────────────┐
   │ 10   20   25   30   40       │      total = 5
   └──────────────────────────────┘

 step 2: mid = total / 2 = 2.  left keeps [0, mid), right takes [mid, total)
   ┌──────────────┐   ┌────────────────────┐
   │ 10   20      │──▶│ 25   30   40       │
   └──────────────┘   └────────────────────┘
     left (2)            right (3)

 step 3: separator = right->keys[0] = 25   ← COPIED, not moved.
         25 stays in the right leaf. It's real data.

 step 4: relink the chain:  right->next = left->next;  left->next = right;

 step 5: hand (25, right) to the parent.
```

### Why `mid = total / 2` is exactly right

`total = LEAF_MAX_ENTRIES + 1`, so `mid = (LEAF_MAX_ENTRIES + 1) / 2`, which is *precisely*
`LEAF_MIN_ENTRIES` as defined in doc 01 §5. So:

- left gets `LEAF_MIN_ENTRIES` — the minimum, exactly. Legal by I4. ✓
- right gets `LEAF_MAX_ENTRIES + 1 − LEAF_MIN_ENTRIES`, which is `≥ LEAF_MIN_ENTRIES`
  because `2 × LEAF_MIN_ENTRIES ≤ LEAF_MAX_ENTRIES + 1` by construction of the ceiling. ✓

Both halves legal, for every `LEAF_MAX_ENTRIES ≥ 2`, with no special cases. That's not luck —
the ceiling in the constant definition was chosen to make this work. **Verify it for
`LEAF_MAX_ENTRIES = 2, 3, 4, 5` on paper**; if you ever change the constants, this is the
property to re-check.

```cpp
// Splits an overfull leaf. Returns {separatorKeyForParent, newRightLeaf}.
std::pair<Key, Node*> splitLeaf(Node* leaf) {
    const int total = (int)leaf->keys.size();          // == LEAF_MAX_ENTRIES + 1
    const int mid   = total / 2;

    Node* right = new Node();
    right->isLeaf = true;

    // move [mid, total) to the right leaf
    right->keys.assign  (std::make_move_iterator(leaf->keys.begin()   + mid),
                         std::make_move_iterator(leaf->keys.end()));
    right->values.assign(std::make_move_iterator(leaf->values.begin() + mid),
                         std::make_move_iterator(leaf->values.end()));

    leaf->keys.resize(mid);
    leaf->values.resize(mid);

    // ---- I7: splice the new leaf into the chain. TWO LINES, ORDER MATTERS. ----
    right->next = leaf->next;
    leaf->next  = right;

    // ---- copy-up: the separator REMAINS in the right leaf --------------------
    return { right->keys.front(), right };
}
```

> **The two relink lines are the #1 forgotten step in every B+Tree anyone writes.** The tree
> stays perfectly valid under I1–I6, `search()` works flawlessly, all your point-lookup tests
> pass — and `scan()` silently returns partial results. Swap the order of those two lines and
> you get a self-loop: `leaf->next = right; right->next = leaf->next;` sets `right->next =
> right`. Your scan hangs forever. Doc 02 §6's `validateLeafChain` catches both, which is why
> it has that `leavesSeen < 1'000'000` cycle guard.

---

## 4. Splitting an internal node — push-up

This is the asymmetric one. Read doc 02 §4 again if the *why* isn't sharp.

```
 INTERNAL_MAX_CHILDREN = 4 (so at most 3 keys). Node is full: 3 keys, 4 children.
 A child split just handed us (separator=70, newNode=N). Insert it → transiently 4 keys, 5 children.

   keys:     [ 30 ][ 50 ][ 70 ][ 90 ]                    K = 4 keys
   children: c₀    c₁    c₂    c₃    c₄                  C = 5 children

   mid = K / 2 = 2   →  keys[2] = 70 is the MEDIAN

   ┌─── left ────────────┐        70        ┌─── right ───────┐
   │ keys:     [30][50]  │   ▲ PUSHED UP    │ keys:     [90]  │
   │ children: c₀ c₁ c₂  │   │ (removed     │ children: c₃ c₄ │
   └─────────────────────┘   │  from both)  └─────────────────┘
     2 keys, 3 children                       1 key, 2 children
```

### The index arithmetic — the four lines that matter

```
   left  keys     = keys[0 .. mid)          → mid keys
   left  children = children[0 .. mid]      → mid+1 children      (note: INCLUSIVE of mid)
   right keys     = keys[mid+1 .. K)        → K-mid-1 keys        (note: SKIPS mid)
   right children = children[mid+1 .. C)    → C-mid-1 children
   separator      = keys[mid]               → moved up, in NEITHER half
```

**`children[0..mid]` inclusive but `keys[0..mid)` exclusive.** That asymmetry — one range
takes the index `mid`, the other doesn't — is the fencepost relationship (I2) in action, and
it is the single most common place to write `mid` where you meant `mid + 1`. If your
validator fires I2 after an internal split, look here first.

Sanity-check the counts: left has `mid` keys and `mid+1` children ✓ I2. Right has `K-mid-1`
keys and `C-mid-1 = K-mid` children ✓ I2 (since `C = K+1`). Total children preserved:
`(mid+1) + (K-mid) = K+1 = C` ✓ — nothing lost, nothing duplicated.

Occupancy check, with `mid = MAX/2` and `INTERNAL_MIN_CHILDREN = (MAX+1)/2`:

| `MAX` | `MIN` | `mid` | left children | right children |
|---|---|---|---|---|
| 3 | 2 | 1 | 2 ✓ | 2 ✓ |
| 4 | 2 | 2 | 3 ✓ | 2 ✓ |
| 5 | 3 | 2 | 3 ✓ | 3 ✓ |
| 6 | 3 | 3 | 4 ✓ | 3 ✓ |
| 170 | 85 | 85 | 86 ✓ | 85 ✓ |

Both halves always meet the minimum. ✓

```cpp
// Splits an overfull internal node. Returns {medianKeyForParent, newRightNode}.
std::pair<Key, Node*> splitInternal(Node* node) {
    const int K   = (int)node->keys.size();        // == INTERNAL_MAX_CHILDREN
    const int mid = K / 2;

    Key median = std::move(node->keys[mid]);       // PUSHED UP — leaves both halves

    Node* right = new Node();
    right->isLeaf = false;

    right->keys.assign    (std::make_move_iterator(node->keys.begin()     + mid + 1),
                           std::make_move_iterator(node->keys.end()));
    right->children.assign(std::make_move_iterator(node->children.begin() + mid + 1),
                           std::make_move_iterator(node->children.end()));

    node->keys.resize(mid);              // drop keys[mid..] INCLUDING the median
    node->children.resize(mid + 1);      // keep children[0..mid] INCLUSIVE

    // NOTE: no `next` pointer work — internal nodes are not in the leaf chain.
    return { std::move(median), right };
}
```

### Leaf vs internal, side by side

Tape this to your monitor:

| | Leaf split | Internal split |
|---|---|---|
| Separator | `right->keys.front()` | `keys[mid]` |
| Fate of separator | **Copied** — stays in right leaf | **Pushed** — removed from both halves |
| Left keeps | `keys[0..mid)` + `values[0..mid)` | `keys[0..mid)` + `children[0..mid]` |
| Right takes | `keys[mid..)` + `values[mid..)` | `keys[mid+1..)` + `children[mid+1..)` |
| `mid` from | `total / 2` where `total = LEAF_MAX+1` | `K / 2` where `K = INTERNAL_MAX` |
| Leaf chain | **Must relink** `next` | Nothing — internals aren't in the chain |
| Total keys | Preserved (`n+1` in, `n+1` out) | Reduced by 1 in this level (median left) |

> **Why the median leaves the level.** In a leaf, `n+1` keys go in and `n+1` come out across
> the two leaves — no data may be lost. In an internal node, `K` keys go in and `K-1` remain
> across the two halves; the missing one moved *up a level*, where it still fences exactly
> the same boundary — just at a coarser granularity. Fences move; records don't.

---

## 5. The root split — the only way a B+Tree grows taller

When the split cascade reaches the root and the root itself splits, there is no parent to
receive the separator. So you **make one**:

```
 Before: root overflows                 After: NEW root, height +1

    ┌──────────────────┐                     ┌──────────┐
    │ 30  50  70  90   │                     │    70    │   ← brand new node
    └──────────────────┘                     └──┬────┬──┘
                                                │    │
                                     ┌──────────┘    └────────────┐
                              ┌────────────┐              ┌──────────┐
                              │  30   50   │              │    90    │
                              └────────────┘              └──────────┘
```

```cpp
void growNewRoot(const Key& separator, Node* oldRoot, Node* newRight) {
    Node* newRoot = new Node();
    newRoot->isLeaf = false;
    newRoot->keys.push_back(separator);
    newRoot->children.push_back(oldRoot);
    newRoot->children.push_back(newRight);
    root = newRoot;
    ++height;
}
```

Three things this makes true, and they're the payoff for the whole design (doc 01 §6):

1. **Every leaf gains exactly one level, simultaneously.** Nothing is walked, nothing is
   rebalanced. Uniform depth (I6) is preserved by construction, in `O(1)`.
2. **The new root has exactly 2 children.** Below `INTERNAL_MIN_CHILDREN` for a non-root
   node — which is why I4 exempts the root. This is *the* reason for the exemption.
3. **Height is the only global state that changes.** No other node in the tree is touched.

> **This is the entire balancing mechanism.** A red-black tree does rotations; an AVL tree
> does rotations and tracks balance factors. A B+Tree does *this*: splits push work upward
> until the top, and the top grows. There is no rotation anywhere in insert. That
> simplicity is why the structure survived 50 years.

---

## 6. The full insert — two formulations

### 6.1 Iterative with a path stack *(matches doc 02 §7.3)*

```cpp
bool insert(const Key& key, const Value& value) {
    if (!root) {                                   // empty tree → one leaf
        root = new Node();
        root->isLeaf = true;
        root->keys.push_back(key);
        root->values.push_back(value);
        return true;
    }

    // ---- 1. descend, recording the path -----------------------------------
    Descent d = descendToLeaf(key);                // doc 03 §4
    Node* leaf = d.leaf;

    // ---- 2. duplicate? overwrite (unique-key policy, doc 02 §7.1) ----------
    int pos = lowerBoundIdx(leaf->keys, key, less);
    if (pos < (int)leaf->keys.size() && eq(leaf->keys[pos], key)) {
        leaf->values[pos] = value;
        return false;                              // false == "was an update"
    }

    // ---- 3. insert into the leaf at sorted position ------------------------
    leaf->keys.insert  (leaf->keys.begin()   + pos, key);
    leaf->values.insert(leaf->values.begin() + pos, value);
    ++entryCount;

    if (!leafOverfull(leaf)) return true;          // the common case: ~1 - 1/f of inserts

    // ---- 4. split the leaf -------------------------------------------------
    auto [sepKey, newNode] = splitLeaf(leaf);

    // ---- 5. unwind the path, propagating splits upward ---------------------
    for (int level = (int)d.path.size() - 1; level >= 0; --level) {
        auto [parent, childIdx] = d.path[level];

        // the new node becomes the child immediately RIGHT of the one we split
        parent->keys.insert    (parent->keys.begin()     + childIdx,     sepKey);
        parent->children.insert(parent->children.begin() + childIdx + 1, newNode);

        if (!internalOverfull(parent)) return true;      // cascade stops here

        std::tie(sepKey, newNode) = splitInternal(parent);
    }

    // ---- 6. we unwound past the root → the root split ----------------------
    growNewRoot(sepKey, root, newNode);
    return true;
}
```

**The two insert indices in step 5 are the last off-by-one.** Read them together:

```
  before:  children:  ...  c[childIdx]  |  c[childIdx+1]  ...
                            (the node we just split)

  after:   keys:      insert sepKey  AT  childIdx
           children:  insert newNode AT  childIdx + 1

  result:  ...  c[childIdx] | sepKey | newNode | c[childIdx+1] ...
                  left half            right half
```

`childIdx` for the key, `childIdx + 1` for the child. The fencepost again (doc 02 §2): the
new fence goes *at* the position of the node that split, and the new node goes immediately
*after* it. Swap them and I5 breaks — keys land in the wrong subtree and become
unfindable — while I2 still passes, so only the validator's I5/I8 check catches it.

### 6.2 Recursive *(the call stack IS the path stack)*

Shorter and, for many people, clearer — and it's the shape you'll want in doc 05, since
delete's rebalance is naturally bottom-up.

```cpp
// Returns a split result if `node` overflowed and split; nullopt otherwise.
std::optional<std::pair<Key, Node*>>
insertInto(Node* node, const Key& key, const Value& value, bool& inserted) {
    if (node->isLeaf) {
        int pos = lowerBoundIdx(node->keys, key, less);
        if (pos < (int)node->keys.size() && eq(node->keys[pos], key)) {
            node->values[pos] = value;
            inserted = false;
            return std::nullopt;
        }
        node->keys.insert  (node->keys.begin()   + pos, key);
        node->values.insert(node->values.begin() + pos, value);
        inserted = true;
        if (!leafOverfull(node)) return std::nullopt;
        return splitLeaf(node);
    }

    int childIdx = upperBoundIdx(node->keys, key, less);
    auto childSplit = insertInto(node->children[childIdx], key, value, inserted);
    if (!childSplit) return std::nullopt;                 // no split below → nothing to do

    auto [sepKey, newNode] = *childSplit;
    node->keys.insert    (node->keys.begin()     + childIdx,     sepKey);
    node->children.insert(node->children.begin() + childIdx + 1, newNode);

    if (!internalOverfull(node)) return std::nullopt;
    return splitInternal(node);
}

bool insert(const Key& key, const Value& value) {
    if (!root) { /* ... same as above ... */ }
    bool inserted = false;
    if (auto split = insertInto(root, key, value, inserted))
        growNewRoot(split->first, root, split->second);
    if (inserted) ++entryCount;
    return inserted;
}
```

**Which to use?** Recursive for v1 — it's harder to get the unwind wrong when the language
does it for you. Recursion depth is `h ≈ 4`, so there's no stack risk. Switch to iterative
in doc 08, where the "path" becomes a list of pinned buffer-pool pages you must unpin in
reverse order — explicit control matters there.

---

## 7. Full worked trace — do this by hand first

`INTERNAL_MAX_CHILDREN = 4`, `LEAF_MAX_ENTRIES = 4`.
Insert: `10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110`

**Stop. Get paper. Work it out.** Compare after. The value of this section is entirely in
the comparison — reading a correct trace teaches you much less than finding out *where* your
version diverged.

<br>

**Inserts 10–40** — root is a leaf, fits:

```
  root(leaf): [10, 20, 30, 40]                      4 == LEAF_MAX, full but NOT overfull
```

**Insert 50** — leaf becomes `[10,20,30,40,50]`, size 5 > 4 → split.
`total=5, mid=2` → left `[10,20]`, right `[30,40,50]`, separator `30` (copy-up).
No parent → root split:

```
                    ┌────────┐
                    │   30   │
                    └─┬────┬─┘
              ┌───────┘    └───────┐
        ┌──────────┐         ┌──────────────┐
        │  10  20  │────────▶│  30  40  50  │──▶ nullptr
        └──────────┘         └──────────────┘
              A                     B
```

**Insert 60** — `60 ≥ 30` → child 1 = B → `[30,40,50,60]`, size 4, not overfull. Done.

**Insert 70** — B → `[30,40,50,60,70]`, size 5 → split. `mid=2` → `[30,40]` | `[50,60,70]`,
separator `50`. Parent (root) was `childIdx=1`, so insert key `50` at index 1, child at
index 2. Root now has 2 keys / 3 children — not overfull:

```
                 ┌────────────┐
                 │   30    50 │
                 └─┬────┬───┬─┘
          ┌────────┘    │   └────────┐
    ┌──────────┐  ┌──────────┐  ┌──────────────┐
    │  10  20  │─▶│  30  40  │─▶│  50  60  70  │──▶ nullptr
    └──────────┘  └──────────┘  └──────────────┘
         A             B              C
```

*(This is the answer to doc 01 §8's exercise. How close were you?)*

**Insert 80** — C → `[50,60,70,80]`, size 4. Done.

**Insert 90** — C → size 5 → split → `[50,60]` | `[70,80,90]`, separator `70`, `childIdx=2`.
Root: keys `[30,50,70]`, 4 children — `4 == INTERNAL_MAX_CHILDREN`, full but **not
overfull**:

```
              ┌──────────────────┐
              │  30    50    70  │
              └─┬───┬─────┬────┬─┘
     ┌──────────┘   │     │    └──────────────┐
 ┌────────┐  ┌────────┐  ┌────────┐  ┌──────────────┐
 │ 10  20 │─▶│ 30  40 │─▶│ 50  60 │─▶│  70  80  90  │──▶ nullptr
 └────────┘  └────────┘  └────────┘  └──────────────┘
     A           B           C              D
```

**Insert 100** — D → `[70,80,90,100]`, size 4. Done.

**Insert 110** — D → `[70,80,90,100,110]`, size 5 → split → `[70,80]` | `[90,100,110]`,
separator `90`, `childIdx=3`.
Insert into root: keys `[30,50,70,90]`, children `[A,B,C,D,E]` → **5 children > 4 →
internal split.**

```
  K = 4, mid = K/2 = 2 → median = keys[2] = 70   (PUSHED UP)

  left:  keys[0..2) = [30, 50]        children[0..2] = [A, B, C]     (3 children)
  right: keys[3..4) = [90]            children[3..5) = [D, E]        (2 children)
```

No parent → **root split**, height becomes 3:

```
                          ┌────────┐
                          │   70   │                        ← new root
                          └─┬────┬─┘
              ┌─────────────┘    └──────────────┐
        ┌───────────┐                      ┌────────┐
        │  30   50  │                      │   90   │
        └─┬───┬───┬─┘                      └─┬────┬─┘
     ┌────┘   │   └────┐              ┌──────┘    └──────┐
 ┌────────┐┌────────┐┌────────┐  ┌────────┐  ┌──────────────┐
 │ 10  20 ││ 30  40 ││ 50  60 │  │ 70  80 │  │ 90 100 110   │
 └────┬───┘└────┬───┘└────┬───┘  └────┬───┘  └──────┬───────┘
      └─────────┴─────────┴───────────┴─────────────┘──▶ nullptr
                      the leaf chain, unbroken
```

**Verify against the invariants** — this is the habit to build:

- I2: root 1 key/2 children ✓; `[30,50]` 2 keys/3 children ✓; `[90]` 1 key/2 children ✓
- I4: non-root internal `[90]` has 2 children `== INTERNAL_MIN_CHILDREN` ✓; every leaf has
  ≥ 2 entries `== LEAF_MIN_ENTRIES` ✓; root has 2 children (exempt) ✓
- I5: `70` fences correctly — everything left of it is `< 70`, everything right is `≥ 70` ✓
- I6: all five leaves at depth 2 ✓
- I7: `10,20,30,40,50,60,70,80,90,100,110` — ascending, complete ✓
- I8: separator `30` = min of subtree B ✓; `50` = min of C ✓; `70` = min of D ✓; `90` = min
  of E ✓ *(all leaf-split separators are copy-ups, so they equal a real key; the internal
  push-up `70` also happens to equal one here — that's coincidence, not a rule)*

---

## 8. Sorted-insert pathology, and bulk loading

Notice something about the trace: **every leaf except the last holds exactly
`LEAF_MIN_ENTRIES = 2` of 4.** Occupancy is 50%. That's not bad luck — it's what
**sequentially increasing inserts** always produce:

```
  insert ascending → always lands in the rightmost leaf → that leaf splits →
  left half is frozen forever at exactly the minimum → repeat
```

You waste **half your memory and half your I/O bandwidth** forever. Random inserts do much
better (~69%, i.e. `ln 2`, a classic result), but sorted insert is *exactly* the case you'll
hit building an index from a sorted term list.

Three fixes, in increasing order of effort:

### 8.1 Skewed split for rightmost-leaf splits

If the splitting leaf has `next == nullptr` (it's the rightmost), split `[all] | [last one]`
instead of down the middle. The frozen left half stays ~100% full. SQLite, InnoDB, and
Postgres all do a version of this ("rightmost leaf optimisation"). ~5 lines:

```cpp
const int mid = (leaf->next == nullptr) ? total - 1 : total / 2;
```

Careful: `total - 1` must still be `≥ LEAF_MIN_ENTRIES`, which holds for
`LEAF_MAX_ENTRIES ≥ 2`. But the *right* half gets 1 entry, below the minimum — legal only
because it's about to receive more appends. If your workload then switches to random inserts
you'd have an under-full leaf; the honest version detects sequential-ness over a window
rather than on one observation. Note this in a comment when you implement it.

### 8.2 Bulk loading — build bottom-up from sorted input

If you have all the data sorted upfront (segment merge, index rebuild — frontier doc 03's
merge is exactly this), don't insert one at a time at all:

```
 1. stream the sorted entries, packing leaves to a chosen FILL FACTOR (e.g. 100% for a
    read-only index, ~70% if you'll insert later), linking `next` as you go
 2. collect (firstKeyOfLeaf, leafPtr) for every leaf → that's the level above's input
 3. pack those into internal nodes the same way
 4. repeat until one node remains → that's the root
```

Cost: **`O(N)`, one pass, zero splits, zero descents, 100% occupancy**, and it produces
leaves in physically sequential order on disk — so subsequent scans stream at full
bandwidth. Compare to `O(N log N)` random-access inserts. For your segment merges this is
strictly the right answer and it's ~60 lines. Doc 07 §8 gives the implementation.

### 8.3 Just accept it

For an in-memory tree with plenty of RAM, 50% occupancy on a sorted load is a wart, not a
crisis. Ship it, measure it, fix it if the numbers say so. That's the discipline from
`complexity-and-measurement.md`.

---

## 9. Failure modes — symptom → cause

The debugging table. When something breaks, start here rather than in the debugger.

| Symptom | Likely cause | Section |
|---|---|---|
| Validator: **I2** (`children != keys+1`) after a split | `children.resize(mid)` instead of `mid + 1`, or `children.begin()+mid` instead of `mid+1` in the right half | §4 |
| Validator: **I5/I8** — keys in wrong subtree | Separator/child insert indices swapped (`childIdx` vs `childIdx+1`) in the unwind | §6.1 |
| Validator: **I6** — differing leaf depths | Root split forgot to update `root`, or a split created a node at the wrong level | §5 |
| Validator: **I7** — chain skips a leaf | `splitLeaf` missing the two relink lines | §3 |
| **`scan()` hangs forever** | Relink lines in wrong order → `right->next = right` self-loop | §3 |
| **Keys equal to a separator are "not found"** | `lower_bound` used for internal routing | doc 03 §2 |
| **Some keys silently lost**, tree otherwise valid | Push-up applied to a leaf split (median removed from the leaf = data deleted) | §3, §4 |
| Nodes exceed `MAX` but validator not run | Overflow check uses `>=` on the wrong constant, or leaf check applied to an internal node | §2 |
| Occupancy stuck at 50%, all leaves minimal | Not a bug — sorted-insert pathology | §8 |
| Crash on the **second** root split | `growNewRoot` reads `root` after reassigning it, or `oldRoot` captured stale | §5 |

---

## 10. Checkpoint before doc 05

1. State the copy-up / push-up rule and **justify** it from separator semantics. (§3, §4)
2. In `splitInternal`, why is it `children.resize(mid + 1)` but `keys.resize(mid)`? (§4)
3. In the unwind, why does the key go at `childIdx` and the child at `childIdx + 1`? (§6.1)
4. Why does insert do amortised `O(1)` structural work despite the cascade? (§1)
5. Why does the root split preserve I6 in `O(1)`, without touching any leaf? (§5)
6. Sorted inserts give 50% occupancy. Explain the mechanism, and name two fixes. (§8)
7. Your scan hangs. One line is wrong. Which, and why? (§3)

**Build now:** `splitLeaf`, `splitInternal`, `growNewRoot`, and `insert` (recursive form).
Then, in your test: insert `1..1000` ascending, `1000..1` descending, and 1000 shuffled
values — calling `validate()` **after every single insert**, not at the end. Then verify
`search()` finds all 1000 in each case, and that a full `scan()` returns exactly 1000 keys in
ascending order. When all three orders pass with per-insert validation, your insert is
correct. That is a real milestone — the hard half of the structure is done.
