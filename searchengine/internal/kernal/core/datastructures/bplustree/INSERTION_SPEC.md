# B+Tree Insertion — Engineering Specification

**Scope:** This document specifies the insertion algorithm **only**. Search, deletion,
rebalancing-on-underflow, merge, borrow, and destruction are out of scope and are referenced
only where insertion depends on them.

**Audience:** An engineer implementing insertion in C++ from this document alone, against the
already-declared types `BPlusTree`, `BPlusTreeNode`, `InternalNode`, `LeafNode`, `RecordID`.

**Status of the structures:** Assumed to exist and not to be redesigned by this document. Where
this specification requires a behaviour that the existing declarations permit but do not enforce,
the requirement is stated as a **rule**, and it is the implementer's obligation to uphold it.

**Frozen decisions.** The following are settled and are not open questions for the implementer.
Every one of them has load-bearing consequences elsewhere in this document; changing any one
requires re-deriving the rules that cite it.

| # | Decision | Governing rules |
|---|---|---|
| 1 | The B+Tree is a generic ordered container with no knowledge of information retrieval | G.5.1 |
| 2 | `RecordID` remains a bare document identifier in the current implementation | G.4.3 |
| 3 | One key owns exactly one posting list | I19 |
| 4 | A posting list holds at most one entry per `documentID` | I20 |
| 5 | The tree represents no multiplicity below the key | I21 |
| 6 | The indexing pipeline aggregates occurrences **before** insertion | G.5.2, P11 |
| 7 | `Insert` receives at most one `(key, documentID)` per key per document | G.5.3 |
| 8 | A duplicate pair reaching the tree is a contract violation, ignored as a no-op | G.5.4, C.16 |
| 9 | Payload and ranking metadata belong to the layer above, never to the tree | G.5.1, G.5.2, G.4.4 |
| 10 | Order means maximum fanout; minimum legal order is 3 | G.3.1, G.3.2 |
| 11 | Separators are right-biased: `keys[i]` is the smallest key of subtree `children[i+1]` | G.3.4 |
| 12 | Overflow is answered by splitting only, never by redistribution to a sibling | G.2 |
| 13 | Insert first, then split; transient one-over-capacity is permitted within the call | G.3.3 |
| 14 | `m_Size` counts `(key, RecordID)` pairs, not distinct keys | I23 |

---

# Goal

## G.1 — What insertion accomplishes

Insertion admits a single pair `(key, record)` into the tree such that, when the operation
returns, the tree is a valid B+Tree that contains that pair, and every structural property listed
in **Tree Invariants** holds again.

Concretely, insertion must accomplish five distinct things, all of them, in the same call:

1. **Placement.** The pair must come to rest in exactly one leaf — the unique leaf whose key range
   covers `key`. There is exactly one correct leaf for any given key; insertion must find that one
   and no other.
2. **Order preservation.** Within that leaf, the key must be placed at the position that keeps the
   leaf's key sequence in strictly ascending order, and the corresponding posting list must be
   placed at the identical index in the parallel value array.
3. **Duplicate absorption.** If the key already exists in the tree, insertion must **not** create a
   second copy of the key. It must instead attach the new `RecordID` to the existing key's posting
   list. A key maps to a *set of `RecordID`s*, not to a single one. Key multiplicity is represented
   by the length of that set, never by repeated entries in the key array.
4. **Capacity repair.** If the placement causes a node to exceed its capacity, insertion must
   restore capacity by splitting, and must repair every consequence of that split: the new node's
   contents, both nodes' parent links, the parent's key and child arrays, the leaf sibling chain,
   and — if the split reached the root — the tree's root pointer and height.
5. **Accounting.** The tree's cached size must reflect the mutation, and only the mutations that
   actually occurred.

## G.2 — What insertion deliberately does *not* do

- It does not rebalance for underflow. Insertion can only increase occupancy.
- It does not redistribute keys to a sibling as an alternative to splitting. Split-only is the
  specified policy. (Redistribution is a legal B+Tree optimisation; it is excluded here so that
  the algorithm has exactly one overflow response and one code path to verify.)
- It does not shrink the tree, never decreases height, and never removes a node.
- It does not reorder the leaf chain beyond splicing in newly created leaves.

## G.3 — Notation and capacity parameters

Let **m** denote `m_Order`, the value passed to the `BPlusTree` constructor and returned by
`Order()`.

**Rule G.3.1 — Order means maximum fanout.** `m` is the maximum number of **children** an internal
node may have. This is the definition used throughout this document. Every capacity bound below is
derived from it. If you later prefer the "order = minimum degree" convention, every formula in this
document changes; do not mix conventions.

**Rule G.3.2 — Minimum legal order is 3.** The constructor must reject `m < 3`. At `m = 2` an
internal node may hold one key and two children, a split of an overfull internal node produces a
child with zero keys, and the tree degenerates. Reject it at construction, not at insert time.

Derived bounds, all of which the implementation must be able to state as named constants or small
inline functions:

| Quantity | Formula | Meaning |
|---|---|---|
| `MaxInternalChildren` | `m` | Upper bound on `InternalNode::children.size()` |
| `MaxInternalKeys` | `m - 1` | Upper bound on an internal node's `keys.size()` |
| `MinInternalChildren` | `ceil(m / 2)` | Lower bound for a **non-root** internal node |
| `MinInternalKeys` | `ceil(m / 2) - 1` | Lower bound for a **non-root** internal node |
| `MaxLeafKeys` | `m - 1` | Upper bound on a leaf's `keys.size()` and `values.size()` |
| `MinLeafKeys` | `ceil((m - 1) / 2)` | Lower bound for a **non-root** leaf |

Integer-arithmetic forms, to avoid floating point: `ceil(m / 2)` is `(m + 1) / 2` in unsigned
integer division; `ceil((m - 1) / 2)` is `m / 2` in unsigned integer division.

**Rule G.3.3 — Transient overflow is permitted.** The algorithm inserts first and splits second.
Between the insertion step and the split step, a node is allowed to hold exactly one key more than
its maximum: `m` keys in a leaf, or `m` keys and `m + 1` children in an internal node. This
transient state must never be observable outside the insert call, must never be observed by
`Validate()`, and must never persist across a return from `Insert`. `std::vector` accommodates this
naturally; do not fight it by pre-splitting on the way down.

**Rule G.3.4 — Separator convention.** For an internal node with keys `k[0..n-1]` and children
`c[0..n]`:

- Every key in the subtree rooted at `c[i]` is **strictly less than** `k[i]`, for all `i` in
  `[0, n-1]`.
- Every key in the subtree rooted at `c[i]` is **greater than or equal to** `k[i-1]`, for all `i` in
  `[1, n]`.

Stated as one sentence: `k[i]` is a lower bound, inclusive, on the subtree `c[i+1]`, and an upper
bound, exclusive, on the subtree `c[i]`. This is the "right-biased" or "smallest-key-of-right-
subtree" convention. Every descent rule and every separator choice in this document depends on it.

## G.4 — Requirements imposed on `KeyType` and `RecordID`

**Rule G.4.1.** `KeyType` must provide a strict weak ordering. The specification assumes a
less-than comparison is available. Every comparison in this document is expressed in terms of
"less than" only; equality of two keys `a` and `b` is defined as *neither is less than the other*.
Do not require `operator==` from `KeyType`, and do not use it, because a type may order
consistently without defining equality identically.

**Rule G.4.2.** `KeyType` must be copy-constructible and copy-assignable. Separator keys are
**copies**, not references, not pointers. A separator that aliases a leaf's storage becomes a
dangling reference the moment that leaf's vector reallocates.

**Rule G.4.3 — `documentID` is the sole identity of a `RecordID`.** `RecordID` is, and in the
current implementation remains, nothing but a document identifier: a single `uint64_t documentID`.
Ordering of entries, and equality of entries, is defined by that field **alone**. The posting list
is maintained sorted by ascending `documentID`.

**Rule G.4.4 — Identity is not payload.** Should the stored element type ever be widened — from
`RecordID` to a richer `Posting` — the added fields are **payload**, not identity. They take no
part in ordering, they take no part in the duplicate test at step C.15, and two entries that agree
on `documentID` are the same entry no matter how their payloads differ. Writing the duplicate test
against the whole element value rather than against `documentID` alone admits two entries for one
document into a single list, which violates I20 and makes every consumer of that list silently
wrong. The tree does not interpret payload, does not merge payload, and does not compare payload.

## G.5 — Layering boundary

**Rule G.5.1 — The B+Tree is a generic ordered container.** It stores an ordered mapping from
`KeyType` to a set of `RecordID`s. It has **no knowledge of information retrieval**. Terms,
documents-as-text, term frequency, positions, fields, scoring, and ranking are not concepts in this
component and must not appear in its logic, its assertions, or its decisions. The phrase "posting
list" survives in `LeafNode::values` and in this document only as the established name for the
per-key `RecordID` set; it confers no retrieval semantics on the container. Nothing in this
specification may be justified by an argument about search behaviour — every rule here must stand
on container correctness alone.

**Rule G.5.2 — Responsibilities above the tree.** The indexing pipeline owns everything the tree
does not:

- Tokenisation, normalisation, and term extraction.
- **Aggregating all occurrences of a term within one document before calling `Insert`.** The
  pipeline counts, positions, and merges; the tree receives the finished result.
- Term frequency, position lists, field masks, payloads, and every ranking input.
- Deciding what a `documentID` denotes, and guaranteeing that it is unique per document.

**Rule G.5.3 — The insertion contract.** `Insert` receives **at most one** `(key, documentID)` pair
per key per document. Later widening of the stored element from `RecordID` to a richer `Posting`
does not relax this: it remains at most one entry per `(key, documentID)`, with the payload arriving
already complete. The tree never merges, accumulates, or recomputes payload, because doing so would
require it to know what the payload means, which Rule G.5.1 forbids.

**Rule G.5.4 — Violations of the contract are ignored, not repaired.** If a duplicate
`(key, documentID)` nevertheless reaches `Insert`, that is a defect in the caller. The tree's
response is to **do nothing** — see step C.16. It does not overwrite, does not merge, does not
error, and does not attempt to guess which of the two the caller meant. Ignoring is chosen over
erroring so that a pipeline bug degrades into a lost update rather than an aborted index build; it
is chosen over merging because merging is precisely the retrieval knowledge Rule G.5.1 forbids the
tree to hold.

---

# Tree Invariants

These are the properties that must hold **before** `Insert` is entered and **after** it returns.
They may be violated only inside the call, and only transiently. `Validate()` is the executable
statement of this section; every invariant below should map to a check there.

**I1 — Order bounds are fixed.** `m_Order` is set at construction and never changes.

**I2 — Intra-node key ordering.** Within any node, leaf or internal, the `keys` vector is sorted in
**strictly** ascending order. No node contains two equal keys.

**I3 — Leaf array parity.** For any leaf, `keys.size()` equals `values.size()`. The posting list at
index `i` belongs to the key at index `i`. These two vectors are parallel arrays and must be
mutated in lockstep, always, without exception.

**I4 — Internal arity relation.** For any internal node, `children.size()` equals `keys.size() + 1`.
This holds for the root internal node too. An internal node with `n` keys has `n + 1` children,
never `n`, never `n + 2`.

**I5 — Separator containment.** For every internal node, the range relation of Rule G.3.4 holds for
every key and every child subtree, transitively, all the way down to leaves.

**I6 — Uniform leaf depth.** Every leaf is at the same distance from the root. This is the defining
property of a B-tree family structure and the reason the tree grows at the root rather than at the
leaves.

**I7 — Root exemption.** The root is exempt from minimum-occupancy bounds. A root that is a leaf may
hold anywhere from `0` to `MaxLeafKeys` keys. A root that is internal must hold at least `1` key and
therefore at least `2` children; an internal root with a single child is illegal and must never be
created by insertion.

**I8 — Non-root occupancy.** Every non-root leaf holds at least `MinLeafKeys` keys. Every non-root
internal node holds at least `MinInternalChildren` children. Insertion never violates this by
itself, because it only adds; but the split step must be checked against it, since a badly chosen
split point can produce an undersized sibling.

**I9 — Maximum occupancy.** No node, root included, exceeds its maximum from the table in G.3,
outside of the transient window of Rule G.3.3.

**I10 — Parent-pointer consistency, downward.** For every internal node `P` and every child `C` in
`P.children`, `C->parent` is `P`. There is no child that points elsewhere and no child that points
to null while having a parent.

**I11 — Parent-pointer consistency, upward.** For every node `N` other than the root, `N->parent` is
non-null, and `N` appears exactly once in `N->parent`'s `children` vector.

**I12 — Root parent is null.** `m_Root->parent` is null. This is the terminating condition for every
upward walk in the algorithm, and its correctness is what stops the recursion.

**I13 — Single parent, no cycles.** Every node has exactly one parent. The structure is a tree: no
node is reachable from itself by following child pointers.

**I14 — Leaf flag correctness.** `isLeaf` is `true` for every `LeafNode` and `false` for every
`InternalNode`, and it is the sole legitimate basis for deciding which downcast to perform.

**I15 — Leaf chain ordering.** Following `next` from the leftmost leaf visits every leaf exactly
once, in ascending key order, and the concatenation of their key vectors is a strictly ascending
sequence covering every key in the tree.

**I16 — Leaf chain symmetry.** For any leaf `L` with non-null `L->next`, `L->next->previous` is `L`.
For any leaf `L` with non-null `L->previous`, `L->previous->next` is `L`. The leftmost leaf's
`previous` is null; the rightmost leaf's `next` is null.

**I17 — No leaf chain crossing.** Internal nodes have no participation in the leaf chain. `next` and
`previous` are never set to anything but a `LeafNode*` or null.

**I18 — Global key uniqueness.** A key appears in at most one leaf, and at most once within it.
Duplicate keys are represented exclusively by multiple entries in one posting list.

**I19 — Posting list ownership.** Each key stored in a leaf owns **exactly one** posting list: the
element of `values` at the key's own index. Not zero, not two, and never a list shared with or
aliased by another key. This is the semantic reading of the parallel-array relation I3; I3 states
that the lengths match, I19 states what the pairing *means*.

**I20 — Posting list well-formedness.** Every posting list is non-empty, sorted in ascending
`documentID` order, and contains **at most one entry for any given `documentID`**. A key with an
empty posting list must not exist; if a posting list would become empty, the key must not have been
created. Uniqueness is judged on `documentID` alone, never on the full `RecordID` value
(Rule G.4.4).

**I21 — The tree represents no multiplicity below the key.** A posting list is a **set** of
`documentID`s, not a bag. The tree has no representation for "this document appears under this key
more than once", and it is not required to acquire one. Any notion of how many times, where, or how
strongly a document relates to a key lives above the tree (see G.5) and is invisible to every
algorithm in this document.

**I22 — Separator provenance (insert-only).** In a tree built by insertion alone, every key stored in
an internal node also exists as a key in some leaf. This is a consequence of the copy-up rule for
leaf splits and the push-up rule for internal splits, and it is a useful assertion during
development. It ceases to hold once deletion is implemented, so gate any assertion on it behind a
build flag rather than baking it into `Validate()` permanently.

**I23 — Size accounting.** `m_Size` equals the total number of `(key, RecordID)` pairs stored in the
tree — that is, the sum of the lengths of all posting lists in all leaves. It is not the number of
distinct keys. Choose this definition and hold to it; `Size()` is meaningless if it drifts.

**I24 — Height consistency.** `Height()` equals the number of edges from the root to any leaf, and
is well-defined precisely because of I6. Height changes only by the root-split step of insertion.

---

# Preconditions

The following are assumed true at the moment `Insert(key, record)` is entered. If any is not
guaranteed by the surrounding system, it must be checked and enforced by the implementation.

**P1 — The tree is valid.** Every invariant in **Tree Invariants** holds. Insertion is not a repair
operation and gives no defined behaviour on a malformed tree.

**P2 — The order is legal.** `m_Order >= 3`, established at construction (Rule G.3.2).

**P3 — The root pointer is meaningful.** `m_Root` is either null, meaning the tree is empty, or it
points to a live node owned by this tree. There is no third state: a non-null root that is
unreachable or foreign is a precondition violation.

**P4 — Empty means fully empty.** If `m_Root` is null then `m_Size` is zero, and there are no
allocated nodes owned by this tree. A null root with a non-zero size is a precondition violation and
indicates a leak from a previous operation.

**P5 — The key is comparable and stable.** `key` can be compared against every key already in the
tree, and comparison is deterministic and consistent for the duration of the call. Keys already
stored in the tree are not mutated by anything during the call.

**P6 — The key is not required to be absent.** Insertion of an already-present **key** is a normal,
expected case, not an error — it is how a second document comes to be associated with that key. The
caller need not check `Contains` first. Distinguish this carefully from an already-present
**`(key, documentID)` pair**, which is a contract violation under Rule G.5.3 and is governed by P11.
An existing key is routine; an existing pair is a caller defect.

**P7 — The record is well-formed.** `record.documentID` is a valid identifier by the caller's
definition. This document defines no reserved or sentinel `documentID` value.

**P8 — Exclusive access.** No other thread is reading or mutating the tree for the duration of the
call. The declared structure carries no synchronisation and none is specified here. Concurrent
insertion into a B+Tree requires latch coupling or an equivalent protocol, which is outside this
scope.

**P9 — No live external references into node storage.** No caller holds a pointer, reference, or
iterator into any node's `keys` or `values` vector across this call. Insertion may cause vector
reallocation and node splits, either of which invalidates such references.

**P10 — Allocation may fail.** Node allocation can throw. The implementation must decide and
document its exception guarantee. This specification requires **at minimum** that allocation of a
new node be performed **before** any structural pointer is rewired, so that a failed allocation
leaves the tree in its pre-call valid state rather than a half-split state. This ordering
requirement is restated at each allocation point in the algorithm.

**P11 — The caller has honoured the aggregation contract.** Per Rule G.5.3, the pipeline has already
combined every occurrence of `key` within the document identified by `record.documentID`, so this
pair is being presented to the tree for the first time. Insertion does not verify this — verification
would cost a posting-list search on every insert whether or not it were needed — but it does detect
the violation for free at step C.15, on the path it must search anyway, and responds per Rule G.5.4.
A caller that cannot honour this contract must deduplicate before calling; it must not rely on the
tree's no-op as an aggregation mechanism, because the no-op keeps the **first** value and silently
discards every later one.

**P12 — The recursion is bounded.** Split propagation ascends at most `Height() + 1` levels. There
is no unbounded recursion, and stack depth is logarithmic in the number of keys.

---

# Postconditions

The following must be true at the moment `Insert(key, record)` returns normally.

**Q1 — The pair is present.** A subsequent `Search(key)` returns a vector containing
`record.documentID`, and `Contains(key)` returns true.

**Q2 — Exactly one key entry.** `key` appears exactly once across all leaves, regardless of whether
this call created it or found it (I18).

**Q3 — The posting list is correct.** The posting list for `key` contains all previously associated
records plus `record`, sorted ascending by `documentID`, with no duplicates (I20).

**Q4 — Every invariant is restored.** All of I1 through I24 hold. `Validate()` returns true.

**Q5 — Size is exact.** If `record` was newly added to the tree, `m_Size` increased by exactly one.
If the identical `(key, documentID)` pair was already present, `m_Size` is unchanged. In no case
does `m_Size` change by more than one in a single call.

**Q6 — Height changed only by root split.** `Height()` is either unchanged, or greater by exactly
one. It increases if and only if a new root was created during this call. It never decreases.

**Q7 — The root is correct.** `m_Root` points to the current root, `m_Root->parent` is null, and the
old root — if it was replaced — is now a child of the new root with its `parent` pointing at the new
root. No node still believes the old root is the root.

**Q8 — The leaf chain is intact.** I15, I16, and I17 hold. Every leaf created during this call has
been spliced into the chain at the correct position, and both of its neighbours point back at it.

**Q9 — No orphans and no leaks.** Every node allocated during the call is reachable from `m_Root`.
No node that was reachable before the call is unreachable after it. Nothing was freed.

**Q10 — Bounded structural work.** The number of splits performed is at most `Height() + 1`, and no
node outside the root-to-leaf path of `key` — with the exception of the immediate sibling links of
newly created leaves — was modified.

**Q11 — Prior data is unchanged.** Every `(key, RecordID)` pair present before the call is still
present and still associated with the same key.

---

# Detailed Algorithm

The algorithm is presented as five procedures, matching the private helpers already declared on
`BPlusTree`:

- **A — `Insert`**, the entry point and root/empty-tree handling.
- **B — `FindLeaf`**, the descent.
- **C — `InsertIntoLeaf`**, key location, duplicate absorption, sorted insertion, overflow detection.
- **D — `SplitLeaf`**, leaf split, separator selection, chain splicing.
- **E — `InsertIntoParent`**, parent insertion, new-root creation.
- **F — `SplitInternal`**, internal split, push-up, parent-pointer reassignment, recursion.

Control flows A → B → C → D → E → F → E → F → … until no node overflows or a new root is created.

Every step below is a single operation. Steps are to be performed in the order written. Where the
order matters for correctness, the reason is stated inline; where no reason is stated, the order is
still the specified order.

---

## Stage A — Entry point: `Insert(key, record)`

**Why this stage exists.** It establishes the two structural preconditions the rest of the algorithm
depends on: that a root exists, and that the root is reachable. It is also the only place where the
empty-tree case is handled, so that no downstream procedure has to test for a null root.

**What data changes.** Possibly `m_Root`, possibly one newly allocated leaf, possibly `m_Size`.

**Invariants preserved.** I7 (a freshly created root leaf with one key is legal), I12 (its parent is
set to null), I14 (its `isLeaf` is true), I16 (its chain pointers are null on both sides), I19 (the single key created gets exactly one
posting list), I23.

### A.1 — Handle the empty tree

1. Read `m_Root`.
2. Test whether `m_Root` is null.
3. If `m_Root` is not null, skip to step **A.2**.
4. Allocate one new `LeafNode`.
5. If allocation fails, propagate the failure without having modified any tree state. At this point
   nothing has been modified, so the tree remains valid and empty.
6. Confirm that the new leaf's `isLeaf` field is `true`. The `LeafNode` constructor sets it; do not
   rely on that silently — the field is public and mutable, and an assertion here costs nothing.
7. Set the new leaf's `parent` to null. It is the root.
8. Set the new leaf's `next` to null. It is the only leaf.
9. Set the new leaf's `previous` to null. It is the only leaf.
10. Confirm the new leaf's `keys` vector is empty.
11. Confirm the new leaf's `values` vector is empty.
12. Append `key` to the new leaf's `keys` vector. The vector now has one element at index `0`.
13. Construct a new, empty posting list — a `std::vector<RecordID>`.
14. Append `record` to that posting list. It now has one element.
15. Append that posting list to the new leaf's `values` vector, at index `0`. The parallel-array
    invariant I3 now holds with both vectors at length one.
16. Assign the new leaf's address to `m_Root`.
17. Increment `m_Size` by one.
18. Return from `Insert`. The empty-tree case is complete and no split is possible, because a single
    key cannot exceed `MaxLeafKeys` for any legal `m >= 3`.

### A.2 — Descend and delegate

19. Invoke **algorithm B, `FindLeaf`**, passing `key`. It returns a pointer to the unique leaf whose
    key range covers `key`.
20. Assert that the returned pointer is non-null. For a non-empty, valid tree, `FindLeaf` cannot
    fail to return a leaf. A null return here means invariant I4, I5, or I10 was already broken
    before the call.
21. Invoke **algorithm C, `InsertIntoLeaf`**, passing the returned leaf, `key`, and `record`.
22. Return from `Insert`. All further work — sorted placement, overflow, splitting, propagation,
    root replacement, and size accounting for the non-empty case — is performed inside algorithm C
    and the procedures it calls.

---

## Stage B — Descent: `FindLeaf(key)`

**Why this stage exists.** Insertion has exactly one correct destination leaf. The separator
convention of Rule G.3.4 makes that leaf uniquely determined by the key. Descent is the process of
consuming that convention, one level at a time, until a leaf is reached.

**What data changes.** Nothing. `FindLeaf` is a pure read. It is declared `const` and must remain so.

**Invariants preserved.** All of them, trivially, since nothing is mutated. What descent *relies*
upon is I5, I6, I4, and I14.

### B.1 — Initialise the cursor

1. Declare a cursor variable of type `BPlusTreeNode<KeyType>*`.
2. Assign `m_Root` to the cursor.
3. If the cursor is null, return null. This case does not arise when called from step A.19, because
   A.1 guarantees a root exists, but `FindLeaf` is also used by `Search`, `Contains`, and
   `RangeSearch`, and must be safe standalone.

### B.2 — Descend one level

4. Test the cursor's `isLeaf` field.
5. If `isLeaf` is `true`, skip to step **B.4**.
6. Downcast the cursor to `InternalNode<KeyType>*`. The `isLeaf` test in step 4 is the sole
   justification for this cast; do not cast before testing.
7. Read the internal node's `keys` vector size; call it `n`.
8. Assert that the internal node's `children` vector size equals `n + 1` (invariant I4). A violation
   here will otherwise manifest as an out-of-range child index several steps later, far from its
   cause.
9. Determine the **descent index** as follows: it is the number of keys in this node that are
   **less than or equal to** `key`. Equivalently, it is the index of the first key that is
   **strictly greater than** `key`, or `n` if no such key exists.
10. Perform this determination by binary search over the `keys` vector, not by linear scan. The
    node holds up to `m - 1` keys and this search executes once per level; linear scan is
    acceptable only for very small `m` and should be a deliberate, measured choice.
11. Express the binary search predicate carefully: you are looking for the first position `i` such
    that `key < keys[i]` is true. This is an **upper bound** search. It is *not* a lower bound
    search. The difference matters precisely when `key` equals a separator: the separator is the
    smallest key of the right subtree (Rule G.3.4), so a key equal to it must descend **right**, and
    only the upper-bound predicate does that.
12. Assert that the descent index is in the range `[0, n]` inclusive.
13. Read the child pointer at the descent index from the `children` vector.
14. Assert that the child pointer is non-null.
15. Assign that child pointer to the cursor.
16. Return to step **B.2**, step 4, and repeat. Each iteration descends exactly one level, so the
    loop terminates after `Height()` iterations by invariant I6.

### B.3 — Termination guarantee

17. Note that the loop cannot cycle: I13 forbids cycles, and every iteration strictly decreases the
    remaining distance to a leaf. No visit-set or iteration cap is required for correctness, though
    a debug-build depth cap of `Height() + 1` is a cheap way to convert a corrupted-tree hang into a
    diagnosable assertion.

### B.4 — Return the leaf

18. Downcast the cursor to `LeafNode<KeyType>*`, justified by the `isLeaf` test at step 4.
19. Return that pointer.

---

## Stage C — Leaf insertion: `InsertIntoLeaf(leaf, key, record)`

**Why this stage exists.** This is where the actual data lands. It performs three distinct
decisions that are frequently and wrongly conflated: *is the key already here*, *where does it go if
not*, and *did adding it overflow the node*. Each is separated below.

**What data changes.** The leaf's `keys` vector, the leaf's `values` vector, and `m_Size`. Nothing
outside the leaf changes in this stage; structural change is deferred to stage D.

**Invariants preserved.** I2 (by inserting at the sorted position), I3 (by mutating both vectors at
the same index in the same step), I18 (by absorbing duplicates rather than appending a second key),
I19 and I20 (by inserting into the key's own posting list at its sorted position), I23 (by the
single conditional
increment).

### C.1 — Locate the key position

1. Read the leaf's `keys` vector size; call it `n`.
2. Assert that the leaf's `values` vector size also equals `n` (invariant I3).
3. Determine the **insertion index** `idx`: the index of the first key in the leaf that is
   **not less than** `key`, or `n` if every key in the leaf is less than `key`.
4. Perform this determination by binary search. This is a **lower bound** search — the first `i`
   such that `keys[i] < key` is false.
5. Note deliberately that this predicate differs from the descent predicate of step B.11. The
   descent needs upper bound; the leaf search needs lower bound. Using the same predicate in both
   places is a defect, and it is the single most common one in this algorithm. Descent must skip
   past an equal separator; leaf search must land **on** an equal key.
6. Assert that `idx` is in the range `[0, n]` inclusive.

### C.2 — Decide: duplicate key or new key

7. Test whether `idx` is less than `n`. If it is not, the key is greater than every key in the leaf,
   so it cannot be a duplicate; skip to step **C.4**.
8. Test whether the key at index `idx` is equal to `key`. Express equality as *neither `keys[idx] <
   key` nor `key < keys[idx]`* (Rule G.4.1). Because step C.1 already established that
   `keys[idx] < key` is false, this reduces to testing that `key < keys[idx]` is also false — one
   comparison, not two.
9. If they are not equal, the key is absent; skip to step **C.4**.
10. If they are equal, the key is present; continue to step **C.3**.

### C.3 — Duplicate key: absorb into the posting list

11. Obtain a reference to the posting list at index `idx` of the leaf's `values` vector. It is the
    posting list belonging to the key at index `idx`, by invariant I3.
12. Assert that this posting list is non-empty (invariant I20).
13. Determine the **posting insertion index**: the index of the first `RecordID` in the posting list
    whose `documentID` is not less than `record.documentID`, or the list's size if none is.
14. Perform this determination by binary search. Note the asymmetry with a node's key array: that
    array is bounded above by `m - 1` and so tolerates a linear scan, whereas a posting list has
    **no upper bound at all** — it grows with the number of documents associated with one key, which
    may be the entire corpus. A linear scan here makes insertion linear in posting-list length, and
    for the most heavily shared keys that single scan dominates the whole operation. This is a
    container-level argument about an unbounded sequence; it does not depend on what the keys mean.
15. Test whether the posting insertion index is less than the posting list's size **and** the
    `documentID` at that index equals `record.documentID`.
16. If both are true, the pair `(key, documentID)` is already present. This is a **violation of the
    insertion contract** of Rule G.5.3: the pipeline was required to aggregate before calling, and
    did not. **The insertion is a no-op.** Do not insert into the posting list. Do not modify the
    existing entry in any way. Do not increment `m_Size`. Return from `InsertIntoLeaf`.
16a. Do not raise an error, and do not return a status. `Insert` returns void and signals failure
    only by exception (step H.6); a contract violation is not an exceptional condition and must not
    abort an index build in progress. Ignoring is the specified response (Rule G.5.4).
16b. Do not overwrite the existing entry with the incoming one, and do not merge them. Both are
    tempting and both are wrong: overwriting silently discards whichever the caller sent first, and
    merging requires the tree to know what the fields mean, which Rule G.5.1 forbids.
16c. In a debug build, assert or log here. Reaching this step means an upstream aggregation bug
    exists, and the tree is the only place in the system positioned to notice. The release build
    must remain silent and must remain a no-op, so the diagnostic must not change behaviour.
16d. Note that step 15 tests `documentID` only, never the whole element value (Rule G.4.4). This is
    already correct for `RecordID` as declared, where the two are the same test, and stays correct
    if the element type is later widened.
16e. Note that this branch cannot have changed the leaf's key count, which is why step C.19 returns
    without an overflow check.
17. Otherwise, insert `record` into the posting list at the posting insertion index, shifting all
    subsequent elements one position to the right. The posting list is now sorted and duplicate-free
    (invariant I20).
18. Increment `m_Size` by one.
19. Return from `InsertIntoLeaf`. No key was added to the `keys` vector, therefore the leaf's key
    count is unchanged, therefore overflow is impossible, therefore no split check is needed. This
    is the only early return in the stage and it must not fall through into the overflow check.

### C.4 — New key: sorted insertion

20. Construct a new, empty posting list.
21. Append `record` to it. It now holds exactly one element and is trivially sorted and
    duplicate-free.
22. Insert `key` into the leaf's `keys` vector at position `idx`, shifting every element from `idx`
    onward one position to the right.
23. Insert the new posting list into the leaf's `values` vector at position `idx`, shifting every
    element from `idx` onward one position to the right.
24. Note that steps 22 and 23 must use the **same** index and must both be performed. Performing one
    without the other breaks invariant I3 and silently misassociates every key from `idx` to the end
    of the leaf with the wrong posting list. Prefer to write these two lines adjacently, with no
    branch or early return between them.
25. Assert that the leaf's `keys` size and `values` size are both `n + 1`.
26. Assert that the leaf's `keys` vector is still strictly ascending. In a debug build, verify the
    two neighbours of `idx` only — comparing `keys[idx-1] < keys[idx]` when `idx > 0`, and
    `keys[idx] < keys[idx+1]` when `idx + 1 < n + 1` — which is sufficient given that the vector was
    sorted before the insertion.
27. Increment `m_Size` by one.

### C.5 — Overflow detection

28. Read the leaf's `keys` vector size again; it is now `n + 1`.
29. Compare it against `MaxLeafKeys`, which is `m - 1`.
30. If the size is less than or equal to `MaxLeafKeys`, the leaf is within capacity. Return from
    `InsertIntoLeaf`. The insertion is complete; proceed to **Stage G, Completion**.
31. If the size is greater than `MaxLeafKeys` — which, given that the leaf held at most
    `MaxLeafKeys` before the insertion, means it now holds exactly `m` keys — the leaf has
    overflowed. Continue to step 32.
32. State the overflow condition as a strict inequality against the maximum, not as an equality
    against a specific count. Writing the test as "size equals `m`" happens to be correct here but
    stops being correct the moment any other code path adds more than one key.
33. Invoke **algorithm D, `SplitLeaf`**, passing the leaf.
34. Return from `InsertIntoLeaf` when `SplitLeaf` returns. `SplitLeaf` is responsible for the split
    and for all upward propagation; `InsertIntoLeaf` performs no work after it.

---

## Stage D — Leaf split: `SplitLeaf(leaf)`

**Why this stage exists.** A leaf holding `m` keys violates invariant I9. Splitting converts one
over-capacity node into two within-capacity nodes, which is the only overflow response this
specification permits. Splitting a leaf necessarily creates a new key range boundary, which the
parent must learn about; that is why this stage ends by calling into stage E rather than returning.

**What data changes.** The overfull leaf's `keys` and `values` vectors are truncated; a new leaf is
allocated and populated; up to four leaf-chain pointers are rewritten; the new leaf's `parent` is
set. The parent's arrays are changed by stage E, not here.

**Invariants preserved.** I2 (both halves stay ascending because the source was ascending and the
split is a contiguous cut), I3 (both vectors are cut at the same index), I8 (the split point is
chosen so both halves meet the minimum), I9 (both halves are within maximum), I15 and I16 (by the
chain splice), I18 (no key is duplicated across the halves, because the cut is exclusive), I19 (whole
posting lists move; none is split).

**Precondition of this stage.** The leaf holds exactly `MaxLeafKeys + 1` keys, that is `m` keys, and
`values` holds `m` posting lists.

### D.1 — Compute the split point

1. Read the leaf's `keys` vector size; call it `K`. By the precondition, `K` equals `m`.
2. Compute the **split index** `s` as `ceil(K / 2)`, which in unsigned integer arithmetic is
   `(K + 1) / 2`.
3. Interpret `s` as follows: the original leaf **retains** the keys at indices `[0, s-1]`, which is
   `s` keys. The new right sibling **receives** the keys at indices `[s, K-1]`, which is `K - s`
   keys.
4. Assert that `s` is at least `MinLeafKeys`, so that the left half is legal (invariant I8).
5. Assert that `K - s` is at least `MinLeafKeys`, so that the right half is legal (invariant I8).
6. Assert that `s` is at least one and `K - s` is at least one, so that neither half is empty.
   For `m = 3`: `K = 3`, `s = 2`, halves of 2 and 1, and `MinLeafKeys` is `1`. Both hold.
7. Note the direction of the bias: the **left** node keeps the extra key when `K` is odd. This is a
   free choice, but it must be made once and applied consistently, because the assertions in steps 4
   and 5 depend on which side is favoured.

### D.2 — Allocate the new sibling

8. Allocate one new `LeafNode`. Call it the **right leaf**; call the original the **left leaf**.
9. Perform this allocation **before** modifying the left leaf in any way. If allocation throws, the
   left leaf is still intact — it is over capacity by one, which violates I9, so this ordering alone
   does not give a strong exception guarantee, but it does confine the damage to a single node with
   no dangling pointers. Achieving a true strong guarantee requires building both halves into fresh
   storage and swapping, which is a legitimate refinement of this step.
10. Confirm the right leaf's `isLeaf` field is `true`.
11. Confirm the right leaf's `keys` and `values` vectors are empty.
12. Reserve capacity in the right leaf's `keys` and `values` vectors for `K - s` elements. This is an
    optimisation, not a correctness requirement, but it avoids repeated reallocation during the
    transfer.

### D.3 — Transfer keys and posting lists

13. For each index `i` from `s` to `K - 1`, in ascending order, append the left leaf's key at index
    `i` to the right leaf's `keys` vector.
14. For each index `i` from `s` to `K - 1`, in ascending order, append the left leaf's posting list
    at index `i` to the right leaf's `values` vector.
15. Perform steps 13 and 14 over the same index range, in the same order. Whether you interleave
    them in one loop or run two loops is immaterial; that they cover identical ranges is not.
16. **Move**, do not copy, the posting lists. A posting list is a `std::vector<RecordID>` and may be
    large. Transferring by move turns an O(list length) copy into a pointer steal. The keys
    themselves may be copied, since `KeyType` is typically small; move them too if `KeyType` is
    expensive.
17. Erase the elements at indices `[s, K-1]` from the left leaf's `keys` vector. The left leaf now
    holds `s` keys.
18. Erase the elements at indices `[s, K-1]` from the left leaf's `values` vector. The left leaf now
    holds `s` posting lists.
19. Note that if step 16 moved the posting lists, the elements erased in step 18 are moved-from
    empty vectors; erasing them is still required, and the erase must not be skipped on the grounds
    that they are empty.
20. Assert that the left leaf's `keys` size equals its `values` size, and that both equal `s`.
21. Assert that the right leaf's `keys` size equals its `values` size, and that both equal `K - s`.
22. Assert that the last key of the left leaf is strictly less than the first key of the right leaf.
    This is the local statement of ordering across the cut.

### D.4 — Splice the new leaf into the leaf chain

23. Read the left leaf's `next` pointer and hold it in a temporary. Call it the **old successor**.
    It may be null.
24. Set the right leaf's `next` to the old successor.
25. Set the right leaf's `previous` to the left leaf.
26. If the old successor is non-null, set the old successor's `previous` to the right leaf.
27. If the old successor is null, do nothing further for it; the right leaf is now the rightmost
    leaf and its `next` is correctly null.
28. Set the left leaf's `next` to the right leaf.
29. Note the ordering constraint across steps 23 to 28: the left leaf's `next` must be read (step 23)
    **before** it is overwritten (step 28). Overwriting first loses the old successor, which is then
    unreachable from the left side and still points backwards at the left leaf, breaking I16 and
    truncating the chain at the splice point.
30. Assert the four-way symmetry: the left leaf's `next` is the right leaf; the right leaf's
    `previous` is the left leaf; if the right leaf's `next` is non-null, its `previous` is the right
    leaf.
31. Note that no leaf other than the left leaf, the right leaf, and the old successor is touched.
    The chain is spliced locally; it is never rebuilt.

### D.5 — Select the separator key

32. Read the key at index `0` of the **right** leaf. This is the smallest key in the right subtree.
33. Make a **copy** of that key into a local variable. Do not take a reference or pointer into the
    right leaf's storage; that storage can reallocate during the parent insertion that follows, and
    the parent must own an independent copy regardless (Rule G.4.2).
34. Designate that copy as the **separator key** for this split.
35. Note that this is a **copy-up**, not a move-up: the key remains in the right leaf as well as
    being placed in the parent. This is the defining difference between a leaf split and an internal
    split, and it exists because a B+Tree stores every data record in a leaf. Removing the key from
    the leaf would delete a record.
36. Verify that this separator satisfies Rule G.3.4 for the pair of nodes it will separate: every
    key in the left leaf is strictly less than it (established by the assertion in step 22), and
    every key in the right leaf is greater than or equal to it (it is the right leaf's minimum, and
    the right leaf is ascending).

### D.6 — Hand off to the parent

37. Invoke **algorithm E, `InsertIntoParent`**, passing the left leaf as the left node, the separator
    key, and the right leaf as the right node.
38. Return from `SplitLeaf` when `InsertIntoParent` returns. Do not set the right leaf's `parent`
    here; stage E owns that assignment, and doing it in both places is how a node ends up pointing
    at a stale parent after a subsequent split.

---

## Stage E — Parent insertion: `InsertIntoParent(left, key, right)`

**Why this stage exists.** A split produces two nodes where the tree previously indexed one. The
level above must be told that the key space it delegated to `left` is now divided at `key` between
`left` and `right`. This stage is the single point at which that fact is recorded, and it is shared
by leaf splits and internal splits alike — which is why it takes `BPlusTreeNode*` rather than a
concrete node type.

**What data changes.** Either `m_Root` and a newly allocated internal node, or an existing parent's
`keys` and `children` vectors. In both branches, the `parent` pointer of `right` — and in the
new-root branch, of `left` as well.

**Invariants preserved.** I4 (one key and one child are added together), I2 and I5 (the separator is
placed at the position matching `left`'s position among the children), I7 and I12 (the new root gets
one key, two children, and a null parent), I10 and I11 (parent pointers are set for both affected
children), I24 (height increases only here).

**Precondition of this stage.** `left` is currently a child of some parent, or is the root. `right`
is newly created, is not yet a child of anything, and its `parent` may hold any value. `key` is a
valid separator: every key beneath `left` is strictly less than it, and every key beneath `right` is
greater than or equal to it.

### E.1 — Detect the root case

1. Read `left`'s `parent` pointer.
2. Test whether it is null.
3. If it is non-null, skip to step **E.3**.
4. Assert that `left` is the same node as `m_Root`. A node with a null parent that is not the root
   violates invariant I11 and indicates that a previous split failed to reparent it. This assertion
   is the cheapest available detector for that class of bug.

### E.2 — Create a new root

5. Allocate one new `InternalNode`. Call it the **new root**.
6. Perform this allocation before modifying `m_Root`, `left`, or `right`, so that a throwing
   allocation leaves the old root in place and reachable.
7. Confirm the new root's `isLeaf` field is `false`.
8. Confirm the new root's `keys` and `children` vectors are empty.
9. Append `key` to the new root's `keys` vector. It now holds exactly one key, at index `0`.
10. Append `left` to the new root's `children` vector. It is now the child at index `0`.
11. Append `right` to the new root's `children` vector. It is now the child at index `1`.
12. Note the ordering of steps 10 and 11: the left child must occupy index `0` and the right child
    index `1`, because Rule G.3.4 requires the subtree below `keys[0]` at `children[0]` and the
    subtree at or above it at `children[1]`. Appending in the wrong order inverts the entire tree
    below this node and every subsequent descent takes the wrong branch.
13. Assert that the new root's `children` size equals its `keys` size plus one — two and one
    respectively (invariant I4).
14. Set the new root's `parent` to null (invariant I12).
15. Set `left`'s `parent` to the new root.
16. Set `right`'s `parent` to the new root.
17. Assign the new root's address to `m_Root`.
18. Note that steps 15, 16, and 17 must all occur. Setting `m_Root` without reparenting `left`
    leaves the old root claiming to be the root, and the next `InsertIntoParent` that reaches it
    will create a *second* new root above it, detaching the first.
19. Note that the tree's height has now increased by exactly one, and that this is the only step in
    the entire algorithm that increases height (postcondition Q6). A B+Tree grows upward from the
    root, never downward from the leaves, and that is what keeps every leaf at the same depth
    (invariant I6): adding a level at the top adds it to every root-to-leaf path simultaneously.
20. Return from `InsertIntoParent`. A node with one key and two children cannot overflow for any
    legal `m >= 3`, so no further propagation is possible or needed. The recursion terminates here.

### E.3 — Insert into the existing parent

21. Downcast `left`'s `parent` to `InternalNode<KeyType>*`. Call it the **parent**.
22. Assert that the parent's `isLeaf` field is `false`. A leaf can never be a parent; if this fires,
    invariant I14 or I10 is broken.
23. Locate the index of `left` within the parent's `children` vector. Call it `p`.
24. Perform this location by scanning the `children` vector for pointer identity with `left`. This
    is an O(m) scan and it is the correct method: the child vector is ordered by key range, not by
    pointer value, so it cannot be binary searched by address. `m` is small and bounded, so this
    scan is not a performance concern. Do not attempt to locate the position by comparing `key`
    against the parent's separators instead — that is equivalent only when the tree is already
    consistent and it silently produces the wrong index when it is not.
25. Assert that `left` was found, that is, that `p` is a valid index. A `left` that is not among its
    own parent's children violates invariant I11.
26. Insert `right` into the parent's `children` vector at position `p + 1`, shifting every element
    from `p + 1` onward one position to the right. The right node must sit immediately after the
    left node, because the two nodes together cover the contiguous key range that `left` alone
    covered before the split.
27. Insert `key` into the parent's `keys` vector at position `p`, shifting every element from `p`
    onward one position to the right.
28. Note the index asymmetry between steps 26 and 27: the child goes to `p + 1` and the key goes to
    `p`. This is not an inconsistency; it follows directly from invariant I4 and Rule G.3.4. With
    `n` keys and `n + 1` children, the key at index `p` separates the child at index `p` from the
    child at index `p + 1`. Placing the key at `p + 1` or the child at `p` corrupts the separator
    alignment for every entry to the right of the insertion point, and — because both vectors remain
    the correct *lengths* — no size assertion will catch it. Searches simply begin returning wrong
    answers for some keys.
29. Set `right`'s `parent` to the parent. This is the assignment deferred from step D.38 and step
    F.29.
30. Note that `left`'s `parent` is already correct and must not be reassigned; `left` did not move.
31. Assert that the parent's `children` size equals its `keys` size plus one (invariant I4).
32. Assert that the parent's `keys` vector is still strictly ascending, checking the neighbours of
    index `p` as in step C.26.
33. Assert that the parent's child at index `p` is `left` and the child at index `p + 1` is `right`.

### E.4 — Detect parent overflow

34. Read the parent's `keys` vector size.
35. Compare it against `MaxInternalKeys`, which is `m - 1`.
36. If the size is less than or equal to `MaxInternalKeys`, the parent is within capacity. Return
    from `InsertIntoParent`. Propagation stops at this level; proceed to **Stage G, Completion**.
37. If the size is greater than `MaxInternalKeys` — meaning it now holds `m` keys and `m + 1`
    children — the parent has overflowed. Continue to step 38.
38. Invoke **algorithm F, `SplitInternal`**, passing the parent.
39. Return from `InsertIntoParent` when `SplitInternal` returns.

---

## Stage F — Internal split: `SplitInternal(node)`

**Why this stage exists.** It is the internal-node analogue of stage D. It differs from a leaf split
in two essential ways: the middle key is **moved** up rather than copied up, and child pointers —
not posting lists — are transferred, which means the moved children's `parent` pointers must be
rewritten.

**What data changes.** The overfull node's `keys` and `children` vectors are truncated; a new
internal node is allocated and populated; the `parent` pointer of every child that moved is
rewritten; the new node's own `parent` is set by stage E.

**Invariants preserved.** I2, I4 (both halves end with children equal to keys plus one), I5 (the
promoted key correctly separates the two halves), I8 (the split point is chosen so both halves meet
the minimum child count), I9, I10 and I11 (by the reparenting loop), I13, I22.

**Precondition of this stage.** The node holds exactly `m` keys and `m + 1` children, and is an
internal node.

### F.1 — Compute the split point

1. Read the node's `keys` vector size; call it `K`. By the precondition, `K` equals `m`.
2. Assert that the node's `children` vector size equals `K + 1`.
3. Compute the **middle index** `mid` as `K / 2` in unsigned integer division, that is, `floor(K/2)`.
4. Interpret `mid` as follows, and note that all three ranges are disjoint and together cover
   everything:
   - The **left** node retains keys at indices `[0, mid-1]` — that is `mid` keys — and children at
     indices `[0, mid]` — that is `mid + 1` children.
   - The key at index `mid` is the **promoted key**. It belongs to neither half.
   - The **right** node receives keys at indices `[mid+1, K-1]` — that is `K - mid - 1` keys — and
     children at indices `[mid+1, K]` — that is `K - mid` children.
5. Verify the arity relation for both halves: the left has `mid` keys and `mid + 1` children; the
   right has `K - mid - 1` keys and `K - mid` children. Both satisfy `children = keys + 1`
   (invariant I4).
6. Assert that `mid + 1` is at least `MinInternalChildren`, so the left half is legal.
7. Assert that `K - mid` is at least `MinInternalChildren`, so the right half is legal.
8. Verify the boundary case by hand for the smallest legal order. For `m = 3`: `K = 3`, `mid = 1`;
   the left half has 1 key and 2 children, the right half has 1 key and 2 children, and
   `MinInternalChildren` is `2`. Both halves are exactly at the minimum. This is the case that
   Rule G.3.2 exists to protect, and it is why `m = 2` is rejected.
9. Assert that both halves have at least one key, so that neither becomes an internal node with a
   single child.

### F.2 — Capture the promoted key

10. Read the key at index `mid`.
11. Make a **copy** of it into a local variable. Call it the **promoted key**. The copy must be taken
    **before** the node's `keys` vector is erased or resized in any way; a reference into that vector
    is dangling the moment the erase occurs.
12. Note that this key will be **removed** from this node — it moves upward rather than being
    duplicated. This is the opposite of the leaf-split rule at step D.35. The reason for the
    difference: an internal key is a routing separator, not data. The record it once pointed at
    still lives in a leaf. Copying it up instead of moving it would leave two copies of the same
    separator at two different levels, and the extra copy in the child would misroute every descent
    that lands on it, because the child's key count and child count would also disagree.
13. Assert that this key is strictly greater than the last key that will remain in the left half —
    the key at index `mid - 1` — whenever `mid` is at least one.
14. Assert that this key is strictly less than the first key that will move to the right half — the
    key at index `mid + 1` — whenever `mid + 1` is less than `K`.

### F.3 — Allocate the new sibling

15. Allocate one new `InternalNode`. Call it the **right node**; call the original the **left node**.
16. Perform this allocation before modifying the left node, for the reason given at step D.9.
17. Confirm the right node's `isLeaf` field is `false`.
18. Confirm the right node's `keys` and `children` vectors are empty.
19. Reserve capacity in both vectors for `K - mid - 1` and `K - mid` elements respectively.

### F.4 — Transfer keys

20. For each index `i` from `mid + 1` to `K - 1`, in ascending order, append the left node's key at
    index `i` to the right node's `keys` vector.
21. Assert that the right node now holds `K - mid - 1` keys.

### F.5 — Transfer children and reparent them

22. For each index `j` from `mid + 1` to `K`, in ascending order, append the left node's child
    pointer at index `j` to the right node's `children` vector.
23. Assert that the right node now holds `K - mid` children.
24. For each child pointer now in the right node's `children` vector, set that child's `parent` to
    the right node.
25. Note that step 24 is mandatory and is the single most frequently omitted step in the entire
    algorithm. The transferred children are not necessarily touched again during this insertion, so
    a stale `parent` produces no immediate symptom. It surfaces much later, when a subsequent
    insertion splits one of those children and step E.21 follows the stale pointer to the **left**
    node, searches for the child among the left node's children at step E.24, and either fails the
    assertion at step E.25 or — if assertions are compiled out — inserts the separator into the
    wrong node entirely.
26. Note that step 24 applies to **every** transferred child, not only the first, and that it applies
    regardless of whether the children are leaves or internal nodes. It is not conditional on
    `isLeaf`.
27. Erase the keys at indices `[mid, K-1]` from the left node's `keys` vector. This range includes
    the promoted key at index `mid`, which is why it starts at `mid` and not at `mid + 1`. The left
    node now holds `mid` keys.
28. Erase the children at indices `[mid+1, K]` from the left node's `children` vector. This range
    starts at `mid + 1`, one higher than the key erase, because the child at index `mid` stays with
    the left node. The left node now holds `mid + 1` children.
29. Note the deliberate index asymmetry between steps 27 and 28 — key range starts at `mid`, child
    range starts at `mid + 1`. Using the same start index for both erases either drops a child that
    should have stayed or keeps a key that should have been promoted, and in either case the arity
    relation of invariant I4 breaks.
30. Assert that the left node's `children` size equals its `keys` size plus one.
31. Assert that the right node's `children` size equals its `keys` size plus one.
32. Assert that every child of the left node has its `parent` set to the left node. These were not
    reassigned because they did not move, but asserting it in a debug build catches damage inflicted
    by earlier operations.
33. Do **not** touch the left node's `parent` pointer. The left node has not moved and still belongs
    to the same parent.
34. Do **not** set the right node's `parent` here. Stage E does it, at step E.16 or step E.29,
    depending on which branch it takes.
35. Note that internal nodes do not participate in the leaf chain. There is no `next` or `previous`
    to splice at this stage. If you find yourself writing chain code here, you have confused stage F
    with stage D.

### F.6 — Propagate upward

36. Invoke **algorithm E, `InsertIntoParent`**, passing the left node as the left node, the promoted
    key, and the right node as the right node.
37. Return from `SplitInternal` when `InsertIntoParent` returns.

---

## Stage G — Recursive propagation and termination

**Why this stage exists.** It is not a separate procedure but a property of the mutual recursion
between stages E and F. It is documented explicitly because the termination argument is what makes
the algorithm correct and bounded, and because "how far up does this go" is the question the
structure of the code answers only implicitly.

**What data changes.** Nothing beyond what stages E and F already change.

**Invariants preserved.** I6 and I24, by the argument in step G.6 below.

1. Stage E calls stage F only when the parent it just modified overflowed.
2. Stage F always calls stage E, exactly once, at its final step.
3. Each such cycle moves one level closer to the root, because stage F operates on a node and stage
   E operates on that node's parent.
4. The cycle terminates in exactly one of two ways, and no others:
   - **By capacity.** Stage E inserts into a parent that does not overflow, and returns at step
     E.36. This is the common case, and it happens at the first ancestor with a free slot.
   - **By root creation.** Stage E encounters a node with a null parent, creates a new root, and
     returns at step E.20. A new root always has one key and two children, which cannot overflow, so
     no further propagation is possible.
5. Therefore the number of splits in a single `Insert` call is at most `Height() + 1`: at most one
   per level, plus the possible root creation (postcondition Q10).
6. Therefore height increases by at most one per insertion, and only via step E.19. Because a new
   root is inserted **above** the entire existing tree, the distance from the root to every leaf
   increases by one **simultaneously**, which is exactly why invariant I6 survives. Any scheme that
   added depth beneath a single leaf would break it irreparably.
7. Note that the recursion may be written as actual recursion or as an explicit loop. The
   specification does not require either. If written recursively, note that the depth is
   `O(log n)` and stack exhaustion is not a practical concern (precondition P12).

---

## Stage H — Completion

**Why this stage exists.** To state precisely what "done" means, and to define what a debug build
should verify before returning.

1. Control returns to `Insert` at step A.22 by unwinding through whichever of stages C, D, E, and F
   were entered.
2. `m_Size` has already been adjusted, exactly once, at step C.18 or step C.27, or not at all in the
   duplicate-record case of step C.16. No other step in the algorithm touches `m_Size`. In
   particular, splits do not change it: a split relocates keys, it does not create or destroy them.
3. `m_Root` is current: unchanged if no root split occurred, or updated at step E.17 if one did.
4. All invariants I1 through I24 hold again.
5. In a debug build, invoke `Validate()` here and assert its result. This is expensive — it is a full
   tree traversal per insertion — so it must be compiled out of release builds and should be
   switchable independently of ordinary assertions, so that bulk-load benchmarks remain meaningful.
6. `Insert` returns. It has no return value; failure is communicated only by exception.

### H.1 — What `Validate()` must check, for insertion's purposes

7. Walk the tree from `m_Root` downward, and confirm at each internal node: the arity relation of
   I4; strictly ascending keys per I2; every child's `parent` pointing back per I10; and, by passing
   down an inclusive lower bound and an exclusive upper bound, the separator containment of I5.
8. Confirm that the root's `parent` is null per I12, and that if the root is internal it has at
   least two children per I7.
9. Record the depth at which each leaf is found and confirm all depths are equal per I6.
10. Confirm every non-root node meets its minimum occupancy per I8, and every node meets its maximum
    per I9.
11. Walk the leaf chain from the leftmost leaf and confirm: every leaf reached by descent is reached
    exactly once by the chain per I15; the symmetry of I16; that the concatenated key sequence is
    strictly ascending, which also establishes global key uniqueness per I18; and that the ends are
    null-terminated.
12. Confirm `keys.size()` equals `values.size()` in every leaf per I3, that every posting list is
    non-empty per I19 and I20, and that every posting list is ascending and free of duplicate
    `documentID` values — tested on `documentID` alone, never on the whole `RecordID` (Rule G.4.4).
13. Sum the posting list lengths and confirm the total equals `m_Size` per I23.

---

# Common Implementation Mistakes

Each entry states the mistake, the invariant it violates, and the observable failure. They are
ordered roughly by how often they occur and how long they take to diagnose.

**M1 — Using the same search predicate for descent and for leaf lookup.**
Descent (step B.11) needs an **upper bound** — the first key strictly greater than the search key.
Leaf lookup (step C.4) needs a **lower bound** — the first key not less than the search key. Using
lower bound during descent sends a key that exactly equals a separator down the **left** subtree,
where by Rule G.3.4 it does not belong. The insert then creates a second copy of that key in a
different leaf, violating I18 and I5. The failure is silent and data-dependent: it manifests only
for keys that happen to be separators, which is a small fraction of keys, and it presents much later
as a `Search` returning an incomplete posting list.

**M2 — Failing to reparent transferred children after an internal split.**
Step F.24. Violates I10 and I11. There is no immediate symptom, because the moved children are not
consulted again during the insertion that moved them. The failure appears many insertions later,
when one of those children splits and step E.21 walks a stale `parent` pointer into the wrong node.
With assertions enabled it fires at step E.25; with assertions disabled it inserts a separator into
an unrelated node, and the tree is silently corrupt from that point on. This is the highest-cost
omission in the algorithm because the distance between cause and symptom is unbounded.

**M3 — Moving the separator up on a leaf split instead of copying it.**
Step D.35. Violates I18 and destroys data. Every B+Tree record lives in a leaf; an internal node
holds only routing information. Removing the key from the right leaf while promoting it deletes that
key's entire posting list from the tree. `Search` for that key returns empty even though the insert
reported success, and `m_Size` no longer matches the sum of posting list lengths, breaking I23.

**M4 — Copying the middle key up on an internal split instead of moving it.**
Step F.12, the mirror of M3. The promoted key remains in the left half while also appearing in the
parent. The left half's key count is then one higher than the split arithmetic assumed, so its
`children.size() == keys.size() + 1` relation breaks — invariant I4. The very next descent through
that node indexes `children` out of range, or reads a child that no longer corresponds to the key it
was compared against.

**M5 — Misaligning the key index and the child index in the parent.**
Step E.28. The child goes to `p + 1`; the key goes to `p`. Getting this wrong keeps both vectors at
their correct **lengths**, so I4 still holds and no size assertion fires. What breaks is I5: every
separator to the right of the insertion point now describes the wrong subtree. Searches for keys in
the affected range return wrong or empty results, and `Validate()` catches it only if it actually
checks separator containment with propagated bounds — which is why step H.7 requires that check
specifically.

**M6 — Overwriting the left leaf's `next` before reading it.**
Step D.29. Violates I15 and I16. The old successor becomes unreachable from the left side of the
chain while still pointing backwards at the left leaf. `RangeSearch` then terminates early or skips
a contiguous block of keys, while point `Search` — which descends rather than scanning — continues
to work perfectly. The discrepancy between the two is the diagnostic signature.

**M7 — Splitting a leaf and forgetting to set the new leaf's `parent`.**
Steps D.38 and E.29 together. Violates I11. The new leaf appears in its parent's `children` vector
but points at null or at garbage. If it points at null, step E.2 misidentifies it as the root on its
next split and creates a spurious second root, detaching an entire subtree — every key beneath it
vanishes from the tree at once. The specification assigns this responsibility to exactly one place,
stage E, precisely so that it cannot be both forgotten and duplicated.

**M8 — Incrementing `m_Size` on a duplicate key, or on a duplicate record.**
Step C.16 versus C.18 versus C.27. Violates I23. `Size()` drifts from reality. Nothing else breaks,
which is what makes this one persist: the tree is structurally perfect and only the counter lies.
Decide once whether size counts records or distinct keys, write it down, and make step H.13 verify
it.

**M9 — Mutating `keys` without mutating `values` at the same index.**
Steps C.22 and C.23, and steps D.17 and D.18. Violates I3. Every key from the insertion point to the
end of the leaf is now paired with its neighbour's posting list. Searches return the wrong
documents, and because the vectors may still have equal length in some variants of this bug, a naive
size-only check misses it. Keep the two mutations adjacent in the source with no branch between
them.

**M10 — Splitting at the wrong index and producing an undersized node.**
Steps D.4, D.5, F.6, and F.7. Violates I8. Insertion alone tolerates it — an undersized node still
searches correctly. Deletion does not: the underflow logic will compute a merge or borrow against a
node that was already below minimum on arrival, and the merge produces an over-capacity node or a
negative count. The bug is planted by insertion and detonates in deletion, so assert the minimums at
split time even though nothing in insertion needs them.

**M11 — Checking for overflow before inserting rather than after.**
Stage C.5 and stage E.4 both check after. A pre-emptive "split if full on the way down" is a
legitimate alternative B-tree strategy, but it is a *different* algorithm with different split
points and different invariants, and it cannot be half-adopted. Mixing the two — splitting on
descent but also splitting on the way back up — splits nodes that did not need splitting, producing
a tree with correct structure but pathologically low occupancy and a height larger than necessary.

**M12 — Assuming the split point produces two equal halves.**
For odd `K` one side gets one more entry than the other. Code that assumes symmetry — for example,
computing the right half's size as `K / 2` and the left's as the same — loses an entry or reads past
the end. Derive both halves from the single split index, never compute them independently.

**M13 — Holding a reference or pointer into a node's vector across an insertion or split.**
Precondition P9 and steps D.33 and F.11. `std::vector` reallocates on growth and invalidates
everything. The separator key in particular must be copied out **before** any erase or insert
touches the vector it came from. This produces a use-after-free that a debug allocator catches
immediately and a release build silently tolerates until it does not.

**M14 — Downcasting without testing `isLeaf`.**
Steps B.6, B.18, E.21. Violates nothing on a correct tree and produces undefined behaviour on any
tree where a bug has already put a leaf where an internal node was expected. The test is one branch;
it converts an unbounded class of undefined behaviour into an assertion at the point of the actual
error.

**M15 — Creating an internal node with one child.**
Step F.9 and invariant I7. Arises from a split point of zero, or from promoting the wrong index. A
one-child internal node is not a B+Tree node; it adds a level of indirection with no routing
information, and any code that assumes `keys.size() >= 1` on an internal node — including the
descent at step B.9 — misbehaves on it.

**M16 — Rebuilding the leaf chain instead of splicing it.**
Step D.31. Walking the chain to find the insertion point turns an O(1) splice into an O(n) scan and
makes bulk insertion quadratic. The correct neighbours are already known: the left leaf is the node
being split, and its current `next` is the right leaf's future `next`. No search is required, ever.

**M17 — Treating the root as if it had a minimum occupancy.**
Invariant I7. A root leaf with one key is legal and normal; a root internal node with two children is
legal and normal. A `Validate()` that applies `MinLeafKeys` uniformly rejects every small, correct
tree, and the resulting "fix" is usually to weaken the check everywhere, which then stops catching
M10.

**M18 — Forgetting that a duplicate-key insert must not fall through to the overflow check.**
Step C.19. The duplicate path adds a record to a posting list and changes no key count, so the leaf
cannot have overflowed. Falling through re-reads the key count — which is unchanged and within
capacity — so on a correct implementation this is merely wasted work. It becomes a real defect only
if the overflow test is written against a stale count captured before the branch, in which case a
leaf that was already at maximum capacity splits spuriously on every duplicate insert.

**M19 — Setting the new root's `parent` to the old root, or leaving it uninitialised.**
Step E.14 and invariant I12. Every upward walk in the algorithm terminates on a null parent. A
non-null root parent makes step E.2 take the wrong branch, so instead of stopping, the algorithm
follows the pointer into a node that is not actually above it — typically the old root, which is now
below it — and the recursion either cycles or corrupts the node it lands on.

**M20 — Testing posting duplication on the whole `RecordID` instead of on `documentID`.**
Step C.15 and Rule G.4.4. Harmless today, because `RecordID` holds nothing but `documentID` and the
two tests coincide exactly. The moment any payload field is added they diverge: two entries for the
same document whose payloads differ compare unequal, the duplicate test misses, and both are
admitted into the list. This violates I20 and I21 at once and defeats the entire reason the list is
a set. The list stays sorted, stays the right length for its contents, and passes every structural
check — only the semantics are wrong. Write the comparison against `documentID` explicitly rather
than relying on a defaulted `operator==`, which will silently widen to include each new field as it
is added. The defect is planted now and detonates at a schema change months later.

**M21 — Letting a defaulted comparison drift as `RecordID` grows.**
The mirror of M20 at the ordering site, step C.13. Posting lists are sorted by `documentID` and by
nothing else. A defaulted `operator<=>` on an extended element type orders by every member in
declaration order, so two entries for the same document would sort by their payload as a tiebreak.
That configuration cannot occur in a well-formed list, so the bug stays invisible until M20 has
already admitted a duplicate — at which point the sort order and the duplicate test disagree, and
binary search begins missing entries that are demonstrably present in the vector.

**M22 — Not preserving the ordering in which a node is modified before its parent is notified.**
Stages D and F both complete all local mutation before calling stage E. Notifying the parent first,
and mutating the child afterwards, leaves a window in which the parent's separator describes a split
that has not happened. This matters not for single-threaded correctness but for any future attempt
to add concurrency, logging, or crash recovery, and reordering it away is a change that is very hard
to reverse later.
