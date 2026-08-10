# 10 — B+Tree II: Insert & Split

> **Build target:** `Insert`, `InsertIntoLeaf`, `SplitLeaf`, `InsertIntoParent`,
> `SplitInternal`. About 300 lines. At the end you insert a million keys into a file, close it,
> reopen it, and every one is still there with `Validate()` passing.
>
> **One genuine algorithmic change from your in-memory version**, and it is not a detail: a
> `std::vector` could hold `m` keys transiently before splitting. **A 4096-byte page cannot.**
> There is no room. §2 is about what replaces "insert first, split second", and it is the most
> important idea in this doc.

---

## 1. `Insert`, top level

```cpp
void DiskBPlusTree::Insert(disk_key_t key, const PostingRef& value) {
    // ---- empty tree: create the first leaf, which is also the root ----
    if (m_Disk.RootPageId() == INVALID_PAGE_ID) {
        page_id_t rootId;
        PageGuard guard = m_Pool.NewGuarded(rootId);

        NodePage leaf = guard.AsNode();
        leaf.Init(NodeType::Leaf);
        leaf.SetKeyCount(1);
        leaf.SetKeyAt(0, key);
        leaf.SetValueAt(0, value);

        guard.Drop();                       // release before touching file metadata
        m_Disk.SetRootPageId(rootId);
        return;
    }

    SearchPath path;
    PageGuard  leafGuard = FindLeaf(key, &path);
    InsertIntoLeaf(std::move(leafGuard), path, key, value);
}
```

Same two branches as your in-memory `Insert`. The first leaf is a leaf, not an internal node —
exactly the point you got wrong once already and now know cold.

`guard.Drop()` before `SetRootPageId` is deliberate. `SetRootPageId` writes the header page
through `DiskManager`, and holding an unrelated pin across a metadata write is the kind of
ordering habit that costs nothing here and prevents deadlock once doc 11 §7's latching exists.

---

## 2. The transient-overflow problem

Your in-memory `InsertIntoLeaf` did this:

```cpp
    leaf->keys.insert(leaf->keys.begin() + index, key);      // now m keys -- one over max
    if (leaf->keys.size() > MaxKeys()) SplitLeaf(leaf);      // fix it afterwards
```

That relies on `std::vector` growing. **A page cannot grow.** `MAX_LEAF_KEYS` is 204 because
205 entries do not physically fit in 4096 bytes. Writing the 205th would run off the end of the
page and into the next frame in the pool — silently corrupting an unrelated page.

Three ways out, and it is worth knowing all three because you will meet all three in real
systems:

| Approach | How | Cost |
|---|---|---|
| **Preemptive split** | Split every full node on the way *down*, so the leaf always has room | Splits nodes that did not need it; wastes space |
| **Scratch buffer** | Build the `MAX+1` sequence in a temporary array, then distribute | One `~4 KB` stack buffer; simple and exact |
| **Reserve a slot** | Size the page for `MAX+1` and never fill the last | Wastes an entry's worth of every page, forever |

We use the **scratch buffer**. It keeps the algorithm identical to the one you already
debugged — insert into a sorted sequence, then split it — with the sequence living in a
temporary rather than in the page.

```cpp
// Small enough for the stack: 205 * 8 + 205 * 12 = 4100 bytes.
struct LeafEntries {
    disk_key_t keys  [MAX_LEAF_KEYS + 1];
    PostingRef values[MAX_LEAF_KEYS + 1];
    std::size_t count = 0;
};
```

> Preemptive splitting is what B-tree textbooks and CLRS teach, and it is genuinely simpler for
> concurrency — you never need to walk back up, so you can release each latch as you descend.
> Doc 11 §7 revisits it for exactly that reason. It is the wrong default here because it splits
> nodes that never needed splitting, lowering average page occupancy and raising the height.

---

## 3. `InsertIntoLeaf`

```cpp
void DiskBPlusTree::InsertIntoLeaf(PageGuard leafGuard, SearchPath& path,
                                   disk_key_t key, const PostingRef& value) {
    NodePage leaf = leafGuard.AsNode();
    const std::size_t index = leaf.LowerBoundIndex(key);
    const std::size_t n     = leaf.KeyCount();

    // ---- duplicate key: overwrite the reference, no structural change ----
    // Different from your in-memory tree, which appended to a posting list. Here the leaf
    // holds a REFERENCE (doc 05 section 3); appending a document rewrites the postings file
    // and then updates these 12 bytes. The tree's shape never changes.
    if (index < n && !(key < leaf.KeyAt(index))) {
        leaf.SetValueAt(index, value);
        return;                                    // guard destructs, unpins dirty
    }

    // ---- room available: shift and insert in place ----
    if (n < MAX_LEAF_KEYS) {
        for (std::size_t i = n; i > index; --i) {
            leaf.SetKeyAt  (i, leaf.KeyAt  (i - 1));
            leaf.SetValueAt(i, leaf.ValueAt(i - 1));
        }
        leaf.SetKeyAt  (index, key);
        leaf.SetValueAt(index, value);
        leaf.SetKeyCount(n + 1);
        return;
    }

    // ---- full: build MAX+1 entries in scratch, then split ----
    SplitLeaf(std::move(leafGuard), path, index, key, value);
}
```

The backwards shift loop is the page equivalent of `vector::insert`. Backwards matters: going
forwards would overwrite each element before reading it. Your compiler will not vectorise this
into a `memmove` because it cannot prove the accessor calls are simple loads and stores — if
this shows up on a profile, doc 12 §5 replaces it with an explicit `memmove` over the raw
bytes, which is legal precisely because §5 of doc 05 laid the keys out contiguously.

> **C++ — the unsigned reverse-loop trap.** Look closely at the loop bounds:
>
> ```cpp
> for (std::size_t i = n; i > index; --i)     // correct
>     leaf.SetKeyAt(i, leaf.KeyAt(i - 1));
> ```
>
> The natural way to write a reverse loop is the one that **hangs forever**:
>
> ```cpp
> for (std::size_t i = n - 1; i >= index; --i)   // INFINITE LOOP when index == 0
>     leaf.SetKeyAt(i + 1, leaf.KeyAt(i));
> ```
>
> `std::size_t` is unsigned. When `i` is 0 and `index` is 0, the condition `i >= index` is true,
> the body runs, `--i` wraps to 18,446,744,073,709,551,615, and the condition is *still* true.
> The loop never ends, and `SetKeyAt` is being called with astronomical indices — so in a
> release build with `assert` compiled out, it writes far outside the page before it hangs.
>
> Run with `n = 3, index = 0`:
>
> ```
>   i=2
>   i=1
>   i=0
>   i=18446744073709551615     <- wrapped; condition still true
>   i=18446744073709551614
>   ...                        INFINITE
> ```
>
> **Compiled with `-Wall -Wextra`, this produces no warning at all.** GCC only diagnoses the
> literal form `i >= 0`; comparing against a *variable* that happens to be zero is invisible to
> it. You get no help from the toolchain here — only from recognising the shape.
>
> **An unsigned counter can never be less than zero, so any loop whose termination depends on
> going below zero is broken.** The condition `i >= 0` is *always true* for unsigned types, and
> compilers warn about it (`-Wtype-limits`) — but `i >= index` hides the same bug behind a
> variable, and no warning fires.
>
> The fix used above is the standard one: **count from `n` down to `index + 1` exclusive, and
> index the source with `i - 1`.** The loop variable never needs to reach zero, so it never
> wraps. Learn this shape; you will write it constantly in code that shifts arrays.
>
> The alternatives, if the shape bothers you: use a signed counter (`std::ptrdiff_t`) and
> compare `>= 0`, or iterate forward over a reversed index (`for (std::size_t k = 0; k < n -
> index; ++k)` with `i = n - k`). All three are correct; the first is the least error-prone
> once you recognise it.

> **C++ — sink parameters.** `void InsertIntoLeaf(PageGuard leafGuard, ...)` takes the guard
> **by value**, and callers pass `std::move(leafGuard)`. For a move-only type this is the
> **sink parameter** idiom: the function is taking ownership, and the signature says so.
>
> The alternatives and why they are worse here:
>
> - `PageGuard&` — the caller cannot tell whether ownership transferred, and the function
>   cannot destroy the guard early.
> - `PageGuard&&` — works, but the parameter inside the function is an *lvalue* named
>   `leafGuard`, so you would still need `std::move` to pass it onward. By-value is simpler and
>   costs one move (a few pointer copies).
>
> Because the parameter is by value, its destructor runs at the end of the function, which is
> what makes the pin release automatically on every path — the same guarantee doc 08 §6 gave
> for locals, now extended across the call boundary.

---

## 4. `SplitLeaf`

```cpp
void DiskBPlusTree::SplitLeaf(PageGuard leafGuard, SearchPath& path,
                              std::size_t insertIndex, disk_key_t key,
                              const PostingRef& value) {
    NodePage        oldLeaf = leafGuard.AsNode();
    const page_id_t oldId   = leafGuard.PageId();

    // ---- 1. Materialise all MAX+1 entries in sorted order ----
    LeafEntries all;
    all.count = MAX_LEAF_KEYS + 1;
    for (std::size_t i = 0, src = 0; i < all.count; ++i) {
        if (i == insertIndex) {
            all.keys[i] = key;  all.values[i] = value;
        } else {
            all.keys[i] = oldLeaf.KeyAt(src);  all.values[i] = oldLeaf.ValueAt(src);  ++src;
        }
    }

    // ---- 2. Split point: same arithmetic as your in-memory tree ----
    const std::size_t splitPoint = (all.count + 1) / 2;      // 103 of 205

    // ---- 3. New right sibling ----
    page_id_t newId;
    PageGuard newGuard = m_Pool.NewGuarded(newId);
    NodePage  newLeaf  = newGuard.AsNode();
    newLeaf.Init(NodeType::Leaf);

    // ---- 4. Distribute ----
    oldLeaf.SetKeyCount(splitPoint);
    for (std::size_t i = 0; i < splitPoint; ++i) {
        oldLeaf.SetKeyAt(i, all.keys[i]);  oldLeaf.SetValueAt(i, all.values[i]);
    }
    newLeaf.SetKeyCount(all.count - splitPoint);
    for (std::size_t i = splitPoint; i < all.count; ++i) {
        newLeaf.SetKeyAt  (i - splitPoint, all.keys[i]);
        newLeaf.SetValueAt(i - splitPoint, all.values[i]);
    }

    // ---- 5. Splice into the leaf chain: old <-> new <-> oldNext ----
    const page_id_t oldNext = oldLeaf.NextLeaf();
    newLeaf.SetNextLeaf(oldNext);
    newLeaf.SetPrevLeaf(oldId);
    oldLeaf.SetNextLeaf(newId);

    if (oldNext != INVALID_PAGE_ID) {
        // The third page. Fetched in its own scope so its pin is released immediately --
        // we are about to recurse upward and will need frames.
        PageGuard nextGuard = m_Pool.FetchGuarded(oldNext);
        nextGuard.AsNode().SetPrevLeaf(newId);
    }

    // ---- 6. Separator is COPIED up. The key stays in the leaf, because leaves hold data. ----
    const disk_key_t separator = newLeaf.KeyAt(0);

    leafGuard.Drop();                     // release both before recursing upward
    newGuard.Drop();

    InsertIntoParent(oldId, separator, newId, path);
}
```

### The pin budget

At peak this function holds **three** pins: the old leaf, the new leaf, and briefly the old
next-leaf. Then it drops to zero before recursing.

That `Drop()` before `InsertIntoParent` is not tidiness. A split can cascade all the way to the
root — at height 5, a naive implementation holding every level would pin 15 pages. With a
16-frame pool it would deadlock, and the error would read "all frames are pinned", pointing at
the buffer pool rather than at the real cause. **Release before you recurse.**

### The copy-up asymmetry, still true

`separator = newLeaf.KeyAt(0)` — a copy. The key remains in the new leaf. This is the same
asymmetry you already know: leaf splits *copy* (leaves hold the data), internal splits *move*
(internal keys are only signposts). Nothing about disk changes it.

---

## 5. `InsertIntoParent` — where the path stack pays off

```cpp
void DiskBPlusTree::InsertIntoParent(page_id_t leftId, disk_key_t key,
                                     page_id_t rightId, SearchPath& path) {
    // ---- the split node was the root: grow a new level ----
    if (path.nodes.empty()) {
        page_id_t newRootId;
        {
            PageGuard rootGuard = m_Pool.NewGuarded(newRootId);
            NodePage  root = rootGuard.AsNode();
            root.Init(NodeType::Internal);
            root.SetKeyCount(1);
            root.SetKeyAt(0, key);
            root.SetChildAt(0, leftId);
            root.SetChildAt(1, rightId);
        }   // guard released before the metadata write

        // THE COMMIT POINT (doc 04 section 7). Until this line the new root is unreachable
        // and the tree is intact under the old root. After it, the new level is live.
        m_Disk.SetRootPageId(newRootId);
        return;
    }

    // ---- otherwise insert into the parent we recorded on the way down ----
    const page_id_t parentId  = path.nodes.back();
    const std::size_t slot    = path.slots.back();     // left's index among parent's children
    path.nodes.pop_back();
    path.slots.pop_back();

    PageGuard parentGuard = m_Pool.FetchGuarded(parentId);
    NodePage  parent      = parentGuard.AsNode();
    const std::size_t n   = parent.KeyCount();

    assert(parent.ChildAt(slot) == leftId && "path stack disagrees with the tree");

    if (n < MAX_INTERNAL_KEYS) {
        for (std::size_t i = n; i > slot; --i)       parent.SetKeyAt  (i, parent.KeyAt  (i - 1));
        for (std::size_t i = n + 1; i > slot + 1; --i) parent.SetChildAt(i, parent.ChildAt(i - 1));
        parent.SetKeyAt  (slot,     key);
        parent.SetChildAt(slot + 1, rightId);
        parent.SetKeyCount(n + 1);
        return;
    }

    SplitInternal(std::move(parentGuard), path, slot, key, rightId);
}
```

### `slot` is why the path stack beats a parent pointer

Doc 09 §4 promised this. To insert the separator you must know *which child* the split node
was. A parent pointer would force an O(340) scan of the parent's children to find `leftId`.
The path stack recorded it on the way down, for free, and the `assert` confirms the two views
of the tree still agree — cheap insurance against a stale path after a concurrent change.

### The commit point

`SetRootPageId` is the only line in the entire insert path that makes the change *visible*. All
the new pages were written before it, but nothing referenced them. Crash before that line: some
orphaned pages and a perfectly intact old tree. Crash after: the new tree.

This is shadow paging, and you have it because reachability funnels through one page. Doc 11 §5
closes the last gap (a torn header write).

---

## 6. `SplitInternal`

```cpp
void DiskBPlusTree::SplitInternal(PageGuard nodeGuard, SearchPath& path,
                                  std::size_t insertSlot, disk_key_t key,
                                  page_id_t newChildId) {
    NodePage        node   = nodeGuard.AsNode();
    const page_id_t nodeId = nodeGuard.PageId();

    // ---- 1. Scratch: MAX+1 keys, MAX+2 children ----
    disk_key_t keys    [MAX_INTERNAL_KEYS + 1];
    page_id_t  children[MAX_INTERNAL_KEYS + 2];

    const std::size_t n = node.KeyCount();
    for (std::size_t i = 0, src = 0; i < n + 1; ++i) {
        keys[i] = (i == insertSlot) ? key : node.KeyAt(src++);
    }
    for (std::size_t i = 0, src = 0; i < n + 2; ++i) {
        children[i] = (i == insertSlot + 1) ? newChildId : node.ChildAt(src++);
    }

    // ---- 2. The middle key MOVES up; it is not left behind in either half ----
    const std::size_t mid       = (n + 1) / 2;
    const disk_key_t  separator = keys[mid];

    // ---- 3. Left keeps [0, mid), right takes (mid, n] ----
    node.SetKeyCount(mid);
    for (std::size_t i = 0; i < mid; ++i)      node.SetKeyAt  (i, keys[i]);
    for (std::size_t i = 0; i <= mid; ++i)     node.SetChildAt(i, children[i]);

    page_id_t newId;
    PageGuard newGuard = m_Pool.NewGuarded(newId);
    NodePage  newNode  = newGuard.AsNode();
    newNode.Init(NodeType::Internal);

    const std::size_t rightKeys = (n + 1) - mid - 1;
    newNode.SetKeyCount(rightKeys);
    for (std::size_t i = 0; i < rightKeys; ++i)  newNode.SetKeyAt  (i, keys[mid + 1 + i]);
    for (std::size_t i = 0; i <= rightKeys; ++i) newNode.SetChildAt(i, children[mid + 1 + i]);

    // NOTE what is absent: no loop reparenting the moved children. Doc 05 section 2 removed
    // the parent pointer precisely so this -- up to 170 page writes -- does not exist.

    nodeGuard.Drop();
    newGuard.Drop();

    InsertIntoParent(nodeId, separator, newId, path);
}
```

### Move, not copy

`keys[mid]` goes up and appears in **neither** half. Contrast `SplitLeaf`, where the separator
stays in the right leaf. If you copied here you would duplicate a separator, the child ranges
would overlap, and searches would drift into the wrong subtree — a silent wrong answer that
`Validate()`'s separator-window check is specifically designed to catch.

### The absent loop

Doc 05 §2 said removing `parentPageId` saves up to 170 page writes per internal split. This is
where the saving is: a loop that would exist, and does not. It is the clearest example in the
series of a *format* decision buying an *algorithmic* win.

---

## 7. What `Validate()` must now catch

Your split code is new; assume it is wrong. Run `Validate()` after **every** insert in the test
below. Specifically it must catch:

- **Wrong split point** → a node below `MIN_*_KEYS`
- **Copy instead of move in `SplitInternal`** → separator-window violation
- **Broken chain splice** → `next`/`prev` disagreeing, or a leaf unreachable from the chain
- **Leaked page** → the §8 accounting check from doc 09: a page neither in the tree nor free
- **Double-allocated page** → the same page appearing twice in the tree

The last two only exist because you are on disk, and they are the two most likely bugs here.

---

## Checkpoint

```cpp
// storage/tests/disktree_insert_test.cpp
#include "../../datastructures/bplustree/DiskBPlusTree.hpp"
#include <cassert>
#include <iostream>
#include <random>
#include <set>

int main(int argc, char** argv) {
    const std::string path = "disktree_insert.db";
    constexpr int N = 200000;

    if (argc > 1 && std::string(argv[1]) == "verify") {
        DiskManager dm(path); BufferPool bp(dm, 256); DiskBPlusTree tree(dm, bp);
        assert(tree.Validate());
        for (int k = 0; k < N; ++k) {
            PostingRef v;
            assert(tree.Search(k, v) && v.offset == static_cast<std::uint64_t>(k) * 10);
        }
        std::cout << "all " << N << " keys survived restart. height=" << tree.Height() << "\n";
        return 0;
    }

    std::remove(path.c_str());
    DiskManager dm(path); BufferPool bp(dm, 256); DiskBPlusTree tree(dm, bp);

    // ---- small tree first: validate after EVERY insert while it is cheap ----
    for (int k = 0; k < 3000; ++k) {
        tree.Insert(k, PostingRef{static_cast<std::uint64_t>(k) * 10, 1});
        assert(tree.Validate());
        assert(bp.PinnedCount() == 0);          // every guard released
    }
    std::cout << "3000 ascending inserts, height=" << tree.Height() << "\n";

    // ---- descending, into a fresh file: exercises the other split direction ----
    {
        std::remove("desc.db");
        DiskManager d2("desc.db"); BufferPool b2(d2, 256); DiskBPlusTree t2(d2, b2);
        for (int k = 3000; k >= 0; --k) {
            t2.Insert(k, PostingRef{static_cast<std::uint64_t>(k), 1});
            assert(t2.Validate());
        }
        for (int k = 0; k <= 3000; ++k) assert(t2.Contains(k));
    }

    // ---- random, at scale ----
    {
        std::remove("rand.db");
        DiskManager d3("rand.db"); BufferPool b3(d3, 256); DiskBPlusTree t3(d3, b3);
        std::mt19937 rng(42);
        std::set<int> model;
        for (int i = 0; i < 50000; ++i) {
            int k = rng() % 100000;
            t3.Insert(k, PostingRef{static_cast<std::uint64_t>(k), 1});
            model.insert(k);
            if (i % 500 == 0) assert(t3.Validate());
        }
        assert(t3.Validate());
        for (int k : model) assert(t3.Contains(k));
        // and nothing that was never inserted
        for (int k = 0; k < 100000; ++k)
            if (!model.count(k)) assert(!t3.Contains(k));
    }

    // ---- the real load, then restart ----
    for (int k = 0; k < N; ++k)
        tree.Insert(k, PostingRef{static_cast<std::uint64_t>(k) * 10, 1});

    assert(tree.Validate());
    std::cout << "inserted " << N << ", height=" << tree.Height()
              << ", pages=" << dm.NumPages()
              << ", hit rate=" << bp.HitRate() * 100 << "%\n";

    // Range scan over the whole tree must return every key, in order.
    assert(tree.RangeSearch(0, N).size() == N);

    bp.FlushAll();
    dm.Sync();
    std::cout << "now run: " << argv[0] << " verify\n";
}
```

Before doc 11, you should have:

- [ ] `Validate()` passing after every insert on the small trees
- [ ] `bp.PinnedCount() == 0` after every insert — check this in the loop, not just at the end
- [ ] Ascending, descending, and random insert orders all valid
- [ ] **The `verify` run passing in a fresh process** with all 200,000 keys
- [ ] Height around 3 for 200,000 keys. If it is much more, your split point is wrong and pages
      are half-empty — go back to §4 step 2.
- [ ] An answer to: *why can't you insert first and split second, the way your in-memory tree
      did?*
- [ ] An answer to: *why does `SplitLeaf` drop both guards before calling `InsertIntoParent`?*

Next: [11 — B+Tree III: Delete & Durability](11-btree-delete-and-durability.md), the last piece
of the tree and the first real crash test.
