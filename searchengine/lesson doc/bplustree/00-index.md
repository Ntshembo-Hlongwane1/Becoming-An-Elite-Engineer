# B+Tree in C++ — Implementation Series

> **Goal:** finish this series able to sit down with an empty `bplustree.hpp` and write a
> correct, efficient, tested B+Tree from memory — in-memory first, then page-backed on disk —
> and know *why* every line is there.
>
> **Optimised for implementation confidence.** Every algorithm is given three times: as a
> picture, as an invariant statement, and as code. The parts people get wrong (split
> arithmetic, the copy-up/push-up asymmetry, separator repair after delete, off-by-one in
> `lower_bound` vs `upper_bound`) get their own sections with worked traces.
>
> **Teaching contract:** the mechanics are taught in full, including code — because the
> mechanics *are* the lesson here, and a hand-wavy split routine teaches nothing. What stays
> yours: the integration into `searchengine`, the disk format decisions, the tuning. Every
> doc ends with checkpoints you write yourself before moving on.

---

## The structure, in one paragraph

A **B+Tree** is a balanced n-ary search tree where **all data lives in the leaves** and the
internal nodes hold only **separator keys** that route you downward. Every node holds
between `⌈m/2⌉` and `m` entries (root exempt), so the tree is **height-balanced by
construction**: it grows *upward at the root*, never sideways. Because a node holds
*hundreds* of keys instead of one, the height for millions of records is 3–4 instead of ~20,
which means 3–4 cache misses (or disk reads) per lookup instead of 20. The leaves are
**linked left-to-right**, so a range scan is *one* descent followed by a pointer-chase
through sorted, contiguous data. Insert overflows a node and **splits** it, pushing a
separator to the parent; delete underflows a node and **borrows from a sibling or merges**
with it, pulling a separator down. That's the whole structure — everything else in this
series is detail on those five italicised words.

---

## The series

| Doc | Title | One-line hook |
|---|---|---|
| [01](01-why-and-the-shape.md) | Why B+Tree, and the Shape | From your `bstree.hpp` to fanout 200; the arithmetic that makes height 3 |
| [02](02-anatomy-and-invariants.md) | Anatomy & Invariants | Node layout, separator semantics, the 8 invariants you validate against |
| [03](03-search-and-scan.md) | Search & Range Scan | Descent, `lower_bound` vs `upper_bound`, the leaf chain, iterators |
| [04](04-insertion-and-splits.md) | Insertion & Splits | Overflow, the copy-up/push-up asymmetry, root growth — with full traces |
| [05](05-deletion-and-rebalance.md) | Deletion & Rebalancing | Underflow, borrow, merge, separator repair, and why lazy delete is legitimate |
| [06](06-cpp-implementation.md) | The C++ Implementation | Full working header: templates, comparators, ownership, exception safety |
| [07](07-efficiency-and-layout.md) | Efficiency & Memory Layout | Vectors → fixed arrays, linear vs binary in-node search, cache lines, bulk load |
| [08](08-disk-and-persistence.md) | Disk & Persistence | Page IDs instead of pointers, serialization, buffer pool, WAL sketch |
| [09](09-build-plan-and-tests.md) | Build Plan & Test Suite | Nine staged checkpoints + the invariant validator + adversarial test cases |

---

## Prerequisites

- Your `internal/kernal/core/datastructures/bstree.hpp` — we start by diagnosing it.
- `lesson doc/frontier/01-memory-hierarchy.md` and `02-storage-layout.md` — doc 07 and 08
  here are the *concrete* version of the page/offset material you already read there.
- `ownership-and-lifecycle-*` — doc 06 leans on it for the destructor and move semantics.
- `complexity-and-measurement.md` — doc 07 leans on it for benchmarking honestly.

## Where this lands in `searchengine`

Two real destinations, both already implied by your frontier docs:

1. **`internal/kernal/core/datastructures/bplustree.hpp`** — sibling to `bstree.hpp` and
   `ringbuffer.hpp`, with `tests/bplustree_test.cpp` in the same style as your existing
   tests. This is the in-memory version, docs 01–07.
2. **The term dictionary** — frontier doc 02 §3 says "the term dictionary stores, per term,
   the byte offset of its posting list." A disk-resident B+Tree keyed on term, valued with
   `(offset, length)`, *is* that structure. Doc 08 is the bridge.

## How to read

Front-to-back, and **write code as you go**. Doc 09 is the staged build plan — if you prefer
to build while learning rather than after, read 09's checkpoint list first, then come back
and go in order. Do not skip 02: every later doc refers to the invariants by number.
