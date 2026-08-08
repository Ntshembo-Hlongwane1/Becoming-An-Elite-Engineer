# 09 — B+Tree I: Descent

> **Build target:** `internal/kernal/core/datastructures/bplustree/DiskBPlusTree.hpp` / `.cpp`
> — the class, `FindLeaf`, `Search`, `Contains`, and `RangeSearch`. About 200 lines. At the end
> you will hand-build a two-level tree on disk, close the file, reopen it in a fresh process,
> and search it.
>
> **This is the payoff doc.** Every line you write here is a line you already wrote in
> `BPlusTree.hpp`. The algorithm does not change at all. What changes is that
> `internalNode->children[index]` becomes "fetch a page, use it, release it" — and that single
> substitution is the whole of on-disk data structures.

---

## 1. The seam

`DiskBPlusTree` knows about keys, ordering, and tree shape. `BufferPool` knows about frames and
residency. `DiskManager` knows about offsets and syscalls. **Nothing crosses.**

Concretely, the rules that keep it honest:

- The tree **never** calls `m_Disk.ReadPage` or `WritePage`. It goes through the pool, always.
  A direct read would bypass the cache and could return stale bytes for a page that is dirty
  in memory — the single worst bug available in this architecture, because both copies look
  plausible.
- The pool **never** interprets page contents. It moves 4096-byte blobs.
- `NodePage` is the only code that knows the byte layout, and it neither reads nor caches.

The one deliberate exception: the tree talks to `DiskManager` for `RootPageId()` /
`SetRootPageId()`, because the root id is file-level metadata, not a page. It is a small,
named, auditable crossing rather than an ambient permission.

---

## 2. The translation table

Keep your `BPlusTree.hpp` open beside this. Every row is a mechanical substitution.

| In-memory (what you wrote) | On-disk (what you write now) |
|---|---|
| `BPlusTreeNode<K>* node` | `page_id_t pageId` + a `PageGuard` |
| `node->keys[i]` | `nodePage.KeyAt(i)` |
| `node->keys.size()` | `nodePage.KeyCount()` |
| `internal->children[i]` | `nodePage.ChildAt(i)` |
| `leaf->values[i]` | `nodePage.ValueAt(i)` → a `PostingRef` |
| `leaf->next` | `nodePage.NextLeaf()` → a `page_id_t` |
| `node->parent` | **the path stack** — see §4 |
| `m_Root` | `m_Disk.RootPageId()` |
| `new LeafNode<K>()` | `m_Pool.NewGuarded(outId)` + `Init(NodeType::Leaf)` |
| `delete node` | `m_Pool.DeletePage(pageId)` |
| `m_Order`, `MaxKeys()` | `MAX_LEAF_KEYS` / `MAX_INTERNAL_KEYS` — **derived, not chosen** |
| *(nothing)* | `UnpinPage` — handled by `PageGuard` |

`UpperBoundIndex` and `LowerBoundIndex` you already ported in doc 05 §6, unchanged in meaning.
Equal still goes right on descent; equal still lands on the slot in a leaf.

---

## 3. The class

```cpp
#pragma once
// internal/kernal/core/datastructures/bplustree/DiskBPlusTree.hpp
#include <vector>
#include "../../storage/BufferPool.hpp"
#include "../../storage/PageGuard.hpp"
#include "../../storage/NodePage.hpp"

class DiskBPlusTree {
public:
    DiskBPlusTree(DiskManager& disk, BufferPool& pool)
        : m_Disk(disk), m_Pool(pool) {}

    // ---- read path (this doc) ----
    bool                    Contains(disk_key_t key);
    bool                    Search(disk_key_t key, PostingRef& out);
    std::vector<PostingRef> RangeSearch(disk_key_t lower, disk_key_t upper);

    // ---- write path (docs 10, 11) ----
    void Insert(disk_key_t key, const PostingRef& value);
    void Remove(disk_key_t key);

    // ---- introspection ----
    bool        Empty()  const { return m_Disk.RootPageId() == INVALID_PAGE_ID; }
    std::size_t Height();
    bool        Validate();
    void        Print(std::ostream& out = std::cout);

private:
    struct SearchPath {
        std::vector<page_id_t>   nodes;   // root .. parent-of-leaf (leaf NOT included)
        std::vector<std::size_t> slots;   // child index taken at each level
    };

    PageGuard FindLeaf(disk_key_t key, SearchPath* path = nullptr);

    DiskManager& m_Disk;
    BufferPool&  m_Pool;
};
```

Note there is no `m_Root` member and no `m_Size`. Both live in the file header, because both
must survive a restart. **Any state a member variable holds is state that dies with the
process** — a rule worth applying every time you are tempted to add a field to a persistent
structure.

`Search` returns `bool` and writes through an out-parameter rather than returning
`std::vector<RecordID>` as your in-memory version did. Doc 05 §3 explains why: the leaf holds a
12-byte *reference* to a posting list, not the list itself.

---

## 4. The path stack — what replaces parent pointers

Doc 05 §2 removed `parentPageId` from the node header because maintaining it costs a page write
per reparented child. The information is recovered for free during descent: to reach the leaf
you passed through its parent, and its parent's parent, and so on.

```cpp
struct SearchPath {
    std::vector<page_id_t>   nodes;   // root .. parent-of-leaf
    std::vector<std::size_t> slots;   // which child index we followed at each level
};
```

`slots` is the part people forget, and it is what makes the stack strictly better than a parent
pointer. When doc 11's merge needs to know "which child of my parent am I?", a parent pointer
gives you the parent and then forces an O(fanout) scan of its children to find yourself. The
path stack already recorded the answer on the way down. **Free, and exact.**

Cost: a `std::vector` allocation per insert. Fix it later with a fixed-size array — the height
is at most 5 or 6, so `std::array<page_id_t, 8>` plus a length covers every tree you will
build, with zero allocation. Leave it as a vector until doc 12 says it matters.

---

## 5. `FindLeaf`

Side by side with yours:

```cpp
// YOUR IN-MEMORY VERSION -- for comparison
LeafNode<KeyType>* FindLeaf(const KeyType& key) const {
    if (m_Root == nullptr) return nullptr;
    BPlusTreeNode<KeyType>* currentNode = m_Root;
    while (!currentNode->isLeaf) {
        InternalNode<KeyType>* internalNode =
            static_cast<InternalNode<KeyType>*>(currentNode);
        std::size_t index = UpperBoundIndex(internalNode->keys, key);
        currentNode = internalNode->children[index];
    }
    return static_cast<LeafNode<KeyType>*>(currentNode);
}
```

```cpp
// THE DISK VERSION -- same five steps
PageGuard DiskBPlusTree::FindLeaf(disk_key_t key, SearchPath* path) {
    page_id_t current = m_Disk.RootPageId();
    if (current == INVALID_PAGE_ID) {
        return {};                                  // empty tree -> empty guard
    }

    if (path != nullptr) { path->nodes.clear(); path->slots.clear(); }

    // Guard against a corrupt file sending us in circles. The in-memory version could not
    // loop because pointers came from allocations; page ids come from bytes on disk, which
    // can be wrong. Never trust a traversal driven by data you did not validate.
    std::size_t depth = 0;
    constexpr std::size_t MAX_DEPTH = 32;

    while (true) {
        if (++depth > MAX_DEPTH) {
            throw std::runtime_error("FindLeaf: tree deeper than " +
                                     std::to_string(MAX_DEPTH) + " -- corrupt file or cycle");
        }

        PageGuard guard = m_Pool.FetchGuarded(current);
        NodePage  node  = guard.AsNodeConst();       // const: descent never writes

        if (node.IsLeaf()) {
            return guard;                            // move out; caller inherits the pin
        }

        const std::size_t index = node.UpperBoundIndex(key);
        const page_id_t   next  = node.ChildAt(index);

        if (next == INVALID_PAGE_ID) {
            throw std::runtime_error("FindLeaf: internal node has INVALID child at slot "
                                     + std::to_string(index));
        }

        if (path != nullptr) {
            path->nodes.push_back(current);
            path->slots.push_back(index);
        }

        current = next;
        // guard destructs here, unpinning the level we just left
    }
}
```

### The three differences that matter

**Only one page is pinned at a time.** The guard from the previous iteration is destroyed at
the closing brace, before the next `FetchGuarded`. So descent through a 5-level tree holds one
frame, not five. That matters with a small pool — and it is a property you get for free from
scoping, not from careful manual bookkeeping.

**The return moves the guard out**, transferring the pin to the caller. The leaf stays resident
for exactly as long as the caller's guard lives. This is the ownership transfer from doc 08 §6
doing real work.

**Corruption is checked.** The in-memory version could not loop forever: its pointers came from
`new`. Here, `ChildAt(i)` returns whatever four bytes are on disk. A corrupt page can point at
its own ancestor, and without the depth cap your engine hangs instead of reporting. **Any
traversal driven by data you did not just validate needs a termination bound.**

---

## 6. `Search` and `Contains`

```cpp
bool DiskBPlusTree::Search(disk_key_t key, PostingRef& out) {
    PageGuard guard = FindLeaf(key);
    if (!guard.Valid()) return false;               // empty tree

    NodePage leaf = guard.AsNodeConst();
    const std::size_t index = leaf.LowerBoundIndex(key);

    // Identical match check to your in-memory Search: bounds first, then equality.
    if (index == leaf.KeyCount() || key < leaf.KeyAt(index)) {
        return false;
    }

    out = leaf.ValueAt(index);
    return true;
}

bool DiskBPlusTree::Contains(disk_key_t key) {
    PostingRef ignored;
    return Search(key, ignored);
}
```

The match check is byte-for-byte the logic you already debugged: `index == KeyCount()` first
(bounds), then `key < KeyAt(index)` (lower bound returns a position, never a match). `||`
short-circuits, so the bounds check protects the second read.

`Contains` delegating to `Search` is fine *here* — unlike the in-memory case, where it would
have copied a whole posting list. A `PostingRef` is 12 bytes. The expensive part is the descent,
and both do exactly one.

---

## 7. `RangeSearch` — the leaf chain, and why it exists

```cpp
std::vector<PostingRef> DiskBPlusTree::RangeSearch(disk_key_t lower, disk_key_t upper) {
    std::vector<PostingRef> result;
    if (upper < lower) return result;

    PageGuard guard = FindLeaf(lower);              // ONE descent, for the whole range
    if (!guard.Valid()) return result;

    std::size_t index = guard.AsNodeConst().LowerBoundIndex(lower);

    while (guard.Valid()) {
        NodePage leaf = guard.AsNodeConst();

        for (; index < leaf.KeyCount(); ++index) {
            if (upper < leaf.KeyAt(index)) {
                return result;                      // past the range
            }
            result.push_back(leaf.ValueAt(index));
        }

        const page_id_t next = leaf.NextLeaf();
        if (next == INVALID_PAGE_ID) break;

        // Move-assign: releases the current leaf's pin, then acquires the next one.
        // Doc 08 section 4.3 -- without the Drop() inside operator=, this leaks a pin
        // per leaf, and a long scan drains the entire pool.
        guard = m_Pool.FetchGuarded(next);
        index = 0;
    }
    return result;
}
```

### Why this is the whole reason for B+Trees

Count the disk reads for a scan of 10,000 keys, leaf fanout 204:

```
  B+Tree, leaf chain:      1 descent (4 reads)  +  49 leaves  =    53 reads
  B-Tree, re-descend:      10,000 x 4 reads                   = 40,000 reads
```

And the 49 leaf reads are *sequential* if the leaves were allocated in order — doc 01 §5 says
that is another 15× on top. This is why all data lives in the leaves and why the leaves are
linked. Every other design decision in the structure serves this one operation.

> **Note the fragility.** Doc 04 §4 warned that LIFO free-list reuse scatters pages. Once your
> leaf chain is physically scrambled, this loop still returns correct results — at random-I/O
> speed. Same code, 15× slower, no error. Doc 11 §6 measures it.

---

## 8. `Height`, `Validate`, `Print`

Port these three from your in-memory versions. They are the same algorithms with the
translation table applied, and you need them working *before* doc 10 — debugging a split
without being able to print the tree is unnecessarily painful.

`Height`: follow `ChildAt(0)` from the root until `IsLeaf()`, counting. One guard at a time.

`Print`: recursive, indented, `NODE [...]` and `LEAF [...]` exactly as your in-memory version.
Fetch a guard per node; the recursion holds one pin per level, so a deep tree with a tiny pool
can exhaust it — which is a genuine finding, not a bug in `Print`.

`Validate`: the same invariants, plus three that are new because they are disk-specific:

1. Every `page_id_t` referenced is `< NumPages()` and not `INVALID_PAGE_ID`.
2. The leaf chain is reachable and its `next`/`prev` links agree in both directions.
3. No page appears twice in the tree, and no page is both in the tree and on the free list.
   Walk the tree collecting ids into a `std::unordered_set`, walk the free list into another,
   and assert the intersection is empty and the union plus page 0 covers `NumPages()`.

That third one is your leak and double-allocation detector, and it will catch the two most
likely bugs in doc 10 and doc 11. Write it now.

---

## Checkpoint

You cannot `Insert` yet, so hand-build a tree — the same technique from the in-memory series,
and the last time you will need it.

```cpp
// storage/tests/disktree_descent_test.cpp
#include "../../datastructures/bplustree/DiskBPlusTree.hpp"
#include <cassert>
#include <iostream>

int main(int argc, char** argv) {
    const std::string path = "disktree_descent.db";

    if (argc > 1 && std::string(argv[1]) == "verify") {
        DiskManager  dm(path);
        BufferPool   bp(dm, 16);
        DiskBPlusTree tree(dm, bp);

        PostingRef v;
        assert(tree.Search(10, v)  && v.offset == 100);
        assert(tree.Search(200, v) && v.offset == 2000);   // equal-to-separator, goes right
        assert(!tree.Contains(5));
        assert(!tree.Contains(999));
        assert(tree.RangeSearch(10, 250).size() == 4);
        assert(tree.Validate());
        std::cout << "survived restart, descent OK\n";
        return 0;
    }

    std::remove(path.c_str());
    DiskManager  dm(path);
    BufferPool   bp(dm, 16);

    //            root [200]
    //           /          \
    //   leaf [10,100]    leaf [200,300]
    page_id_t leftId, rightId, rootId;
    {
        PageGuard l = bp.NewGuarded(leftId);
        NodePage n = l.AsNode();
        n.Init(NodeType::Leaf);
        n.SetKeyCount(2);
        n.SetKeyAt(0, 10);  n.SetValueAt(0, PostingRef{100, 1});
        n.SetKeyAt(1, 100); n.SetValueAt(1, PostingRef{1000, 1});
    }
    {
        PageGuard r = bp.NewGuarded(rightId);
        NodePage n = r.AsNode();
        n.Init(NodeType::Leaf);
        n.SetKeyCount(2);
        n.SetKeyAt(0, 200); n.SetValueAt(0, PostingRef{2000, 1});
        n.SetKeyAt(1, 300); n.SetValueAt(1, PostingRef{3000, 1});
    }
    {   // link the chain -- RangeSearch depends on it
        PageGuard l = bp.FetchGuarded(leftId);
        PageGuard r = bp.FetchGuarded(rightId);
        l.AsNode().SetNextLeaf(rightId);
        r.AsNode().SetPrevLeaf(leftId);
    }
    {
        PageGuard root = bp.NewGuarded(rootId);
        NodePage n = root.AsNode();
        n.Init(NodeType::Internal);
        n.SetKeyCount(1);
        n.SetKeyAt(0, 200);              // separator: equal goes RIGHT
        n.SetChildAt(0, leftId);
        n.SetChildAt(1, rightId);
    }
    dm.SetRootPageId(rootId);

    DiskBPlusTree tree(dm, bp);
    assert(tree.Height() == 2);
    assert(tree.Validate());

    PostingRef v;
    assert(tree.Search(10, v)  && v.offset == 100);
    assert(tree.Search(300, v) && v.offset == 3000);
    assert(tree.Search(200, v) && v.offset == 2000);   // THE boundary case
    assert(!tree.Contains(150));
    assert(tree.RangeSearch(0, 1000).size() == 4);
    assert(tree.RangeSearch(100, 200).size() == 2);

    // Descent must hold exactly one frame at a time, and release it.
    assert(bp.PinnedCount() == 0);

    bp.FlushAll();
    dm.Sync();
    std::cout << "descent OK -- now run: " << argv[0] << " verify\n";
}
```

Before doc 10, you should have:

- [ ] All assertions passing, **including the `verify` run in a separate process** — this is
      the first time your *tree* survives a restart
- [ ] `bp.PinnedCount() == 0` after every operation. If not, you have a leaked guard; find it
      now, because doc 10 triples the number of guards in flight.
- [ ] `Validate()` including the page-accounting check from §8
- [ ] An answer to: *why does the tree never call `DiskManager::ReadPage` directly?*
- [ ] An answer to: *what does the path stack give you that a parent pointer does not?*
- [ ] An answer to: *why does `FindLeaf` need a depth cap when your in-memory version did not?*

Next: [10 — B+Tree II: Insert & Split](10-btree-insert-on-pages.md), where the tree starts
allocating its own pages.
