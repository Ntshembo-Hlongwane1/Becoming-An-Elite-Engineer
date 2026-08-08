# 11 — B+Tree III: Delete & Durability

> **Build target:** `Remove`, `FixUnderflow`, `BorrowFromLeft`, `BorrowFromRight`, `MergeNodes`,
> `ShrinkRoot`, plus a checkpoint protocol and a torn-write-proof header. About 350 lines. At
> the end you will kill the process mid-workload with `TerminateProcess`, reopen the file, and
> get a valid tree.
>
> **Delete is where storage engines actually break.** Insert only ever adds pages; delete frees
> them, and a page freed while still referenced is the one bug that silently returns another
> node's data as your answer. §7's page-accounting check is not optional.

---

## 1. `Remove`

```cpp
void DiskBPlusTree::Remove(disk_key_t key) {
    if (m_Disk.RootPageId() == INVALID_PAGE_ID) return;

    SearchPath path;
    PageGuard  leafGuard = FindLeaf(key, &path);
    if (!leafGuard.Valid()) return;

    NodePage leaf = leafGuard.AsNode();
    const std::size_t index = leaf.LowerBoundIndex(key);
    const std::size_t n     = leaf.KeyCount();

    if (index == n || key < leaf.KeyAt(index)) {
        return;                                    // not present
    }

    // Shift the tail down over the removed slot. Forwards this time -- we are moving data
    // toward the front, so reading ahead of writing is correct.
    for (std::size_t i = index; i + 1 < n; ++i) {
        leaf.SetKeyAt  (i, leaf.KeyAt  (i + 1));
        leaf.SetValueAt(i, leaf.ValueAt(i + 1));
    }
    leaf.SetKeyCount(n - 1);

    // A stale separator upstairs is HARMLESS -- exactly as in your in-memory tree. Separators
    // are routing values, not data. A search for the deleted key still descends to this leaf
    // and fails the match check. Only merges and borrows must repair them.
    FixUnderflow(std::move(leafGuard), path);
}
```

Note the shift direction flipped from insert. Insert shifts backwards (from the end), delete
shifts forwards (from the gap). Both are "read before you overwrite"; the direction depends on
which way the data moves.

---

## 2. `FixUnderflow`

```cpp
void DiskBPlusTree::FixUnderflow(PageGuard nodeGuard, SearchPath& path) {
    NodePage node = nodeGuard.AsNodeConst();

    // ---- the root is exempt from occupancy rules ----
    if (path.nodes.empty()) {
        const page_id_t rootId = nodeGuard.PageId();
        const bool      isLeaf = node.IsLeaf();
        const std::size_t count = node.KeyCount();
        page_id_t onlyChild = INVALID_PAGE_ID;
        if (!isLeaf && count == 0) onlyChild = node.ChildAt(0);

        nodeGuard.Drop();                          // release before freeing the page
        ShrinkRoot(rootId, isLeaf, count, onlyChild);
        return;
    }

    if (node.KeyCount() >= node.MinKeys()) {
        return;                                    // still legal
    }

    const page_id_t   parentId = path.nodes.back();
    const std::size_t slot     = path.slots.back();

    PageGuard parentGuard = m_Pool.FetchGuarded(parentId);
    NodePage  parent      = parentGuard.AsNode();

    const page_id_t leftId  = (slot > 0)
        ? parent.ChildAt(slot - 1) : INVALID_PAGE_ID;
    const page_id_t rightId = (slot < parent.KeyCount())
        ? parent.ChildAt(slot + 1) : INVALID_PAGE_ID;

    // ---- try to borrow: cheaper than merging, and keeps the tree shape stable ----
    if (leftId != INVALID_PAGE_ID) {
        PageGuard sib = m_Pool.FetchGuarded(leftId);
        if (sib.AsNodeConst().KeyCount() > node.MinKeys()) {
            BorrowFromLeft(nodeGuard, sib, parentGuard, slot);
            return;
        }
    }
    if (rightId != INVALID_PAGE_ID) {
        PageGuard sib = m_Pool.FetchGuarded(rightId);
        if (sib.AsNodeConst().KeyCount() > node.MinKeys()) {
            BorrowFromRight(nodeGuard, sib, parentGuard, slot);
            return;
        }
    }

    // ---- no sibling can spare a key: merge. Always fold right into left, so the
    //      survivor keeps its slot in the parent and the parent's arrays shift once. ----
    if (leftId != INVALID_PAGE_ID) {
        PageGuard left = m_Pool.FetchGuarded(leftId);
        MergeNodes(left, nodeGuard, parentGuard, slot);      // node is the right of the pair
    } else {
        PageGuard right = m_Pool.FetchGuarded(rightId);
        MergeNodes(nodeGuard, right, parentGuard, slot + 1);
    }

    // The merge removed a key from the parent, which may now underflow. Recurse upward.
    nodeGuard.Drop();
    path.nodes.pop_back();
    path.slots.pop_back();
    FixUnderflow(std::move(parentGuard), path);
}
```

Structurally identical to your in-memory `FixUnderflow`. The differences are all mechanical:
sibling ids come from `parent.ChildAt` rather than `parent->children[]`, and the recursion
carries the path stack instead of following `->parent`.

### The pin budget, again

At the deepest point this holds **three** pins: node, sibling, parent. The sibling guards are
scoped inside `if` blocks so a failed borrow releases immediately rather than holding a frame
through the merge. With a small pool and a deep tree this is the difference between working and
"all frames are pinned".

---

## 3. Borrow and merge

Same rotations as your in-memory version. Two things are new, and both are disk-specific.

```cpp
void DiskBPlusTree::MergeNodes(PageGuard& left, PageGuard& right,
                               PageGuard& parentGuard, std::size_t rightSlot) {
    NodePage l = left.AsNode();
    NodePage r = right.AsNodeConst();
    NodePage p = parentGuard.AsNode();

    const std::size_t ln = l.KeyCount();
    const std::size_t rn = r.KeyCount();

    if (l.IsLeaf()) {
        for (std::size_t i = 0; i < rn; ++i) {
            l.SetKeyAt  (ln + i, r.KeyAt  (i));
            l.SetValueAt(ln + i, r.ValueAt(i));
        }
        l.SetKeyCount(ln + rn);

        // ---- NEW #1: unlink from the leaf chain, including the far side ----
        const page_id_t rightNext = r.NextLeaf();
        l.SetNextLeaf(rightNext);
        if (rightNext != INVALID_PAGE_ID) {
            PageGuard nextGuard = m_Pool.FetchGuarded(rightNext);
            nextGuard.AsNode().SetPrevLeaf(left.PageId());
        }
    } else {
        // The separator comes DOWN from the parent to sit between the two key runs.
        // Without it the merged node would be missing a boundary between its two halves.
        l.SetKeyAt(ln, p.KeyAt(rightSlot - 1));
        for (std::size_t i = 0; i < rn; ++i)  l.SetKeyAt  (ln + 1 + i, r.KeyAt  (i));
        for (std::size_t i = 0; i <= rn; ++i) l.SetChildAt(ln + 1 + i, r.ChildAt(i));
        l.SetKeyCount(ln + 1 + rn);
    }

    // ---- remove the separator and the right child from the parent ----
    const std::size_t pn = p.KeyCount();
    for (std::size_t i = rightSlot - 1; i + 1 < pn; ++i) p.SetKeyAt  (i, p.KeyAt  (i + 1));
    for (std::size_t i = rightSlot;     i + 1 <= pn; ++i) p.SetChildAt(i, p.ChildAt(i + 1));
    p.SetKeyCount(pn - 1);

    // ---- NEW #2: the page must be RETURNED, not just forgotten ----
    const page_id_t deadId = right.PageId();
    right.Drop();                          // unpin BEFORE deleting; DeletePage refuses pinned
    m_Pool.DeletePage(deadId);             // page table -> free frame -> disk free list
}
```

### New #1 — the chain has three participants

In memory, `delete right` made the node vanish and only `left->next` needed fixing. On disk the
`prev` pointer of `right`'s successor also points at a page that is about to be freed. Miss it
and a backwards scan walks into a page on the free list — which will shortly be handed out as
something else entirely.

Your `Validate()` leaf-chain check catches this, which is why doc 09 §8 asked for both
directions.

### New #2 — `right.Drop()` before `DeletePage`

`DeletePage` refuses to free a pinned page (doc 06 §9), and correctly so: freeing memory
someone holds a pointer into is the bug the pin count exists to prevent. The explicit `Drop()`
is the handoff. Forgetting it throws — noisily and immediately, which is the good outcome.

---

## 4. Durability: what "saved" means

From doc 03 §7, `fsync` costs 0.5–10 ms and your inserts cost microseconds. Calling it per
insert makes the engine ~1000× slower. So durability is a **batch** property.

```cpp
void DiskBPlusTree::Checkpoint() {
    m_Pool.FlushAll();      // every dirty frame -> the file (kernel page cache)
    m_Disk.Sync();          // flush header, then fsync -> the device
}
```

**The order is the entire protocol.** Data pages must be durable before the header that
references them. Reverse it and a crash between the two leaves a header pointing at pages that
never made it — a tree confidently referencing garbage.

| Guarantee | Cost | How |
|---|---|---|
| Nothing survives a crash | free | Never call `Checkpoint` |
| Everything up to the last checkpoint | ~5 ms per checkpoint | Call it every N ops or T seconds |
| Every committed operation | ~5 ms per op | WAL — see below |

Levels 1 and 2 you have. Level 3 needs a write-ahead log, and the sketch is worth knowing:
append `(operation, page, before, after)` to a separate file, `fsync` **that** (a sequential
append, far cheaper than scattered page writes), then apply the change to pages at your leisure.
On restart, replay the log from the last checkpoint. The insight is that **an append-only
sequential log is cheap to make durable, and random page writes are not** — so you make the log
authoritative and the pages a cache of it.

That is the entire idea behind WAL in Postgres, redo logs in InnoDB, and journals in ext4.

---

## 5. The torn header, and the fix

Doc 04 §7 flagged the remaining hole: a single 4096-byte write is *usually* atomic but not
guaranteed. A torn header is unrecoverable — it is the only path to your data.

The fix is fifteen lines. Keep **two** header slots and alternate, each with a checksum and a
generation counter:

```
   page 0: header A   (generation 7,  checksum ok)
   page 1: header B   (generation 8,  checksum ok)   <- newest valid, use this
```

```cpp
void DiskManager::FlushHeader() {
    if (!m_HeaderDirty) return;

    m_Header.generation++;
    m_Header.checksum = 0;
    m_Header.checksum = Crc32(&m_Header, sizeof(m_Header));

    Page p;
    EncodeHeader(m_Header, p);
    WritePage(m_Header.generation % 2, p);        // alternate between page 0 and page 1
    m_HeaderDirty = false;
}

FileHeader DiskManager::LoadBestHeader() {
    FileHeader a = TryLoad(0), b = TryLoad(1);
    const bool aOk = ChecksumValid(a), bOk = ChecksumValid(b);

    if (aOk && bOk) return (a.generation > b.generation) ? a : b;
    if (aOk) return a;
    if (bOk) return b;
    throw std::runtime_error("both headers corrupt -- file is unrecoverable");
}
```

Why it works: a torn write damages **one** slot. The other still holds the previous generation,
which is a fully consistent older state pointing at pages that are all still present (the free
list from that generation never reused them, because the newer generation never became
visible). You lose the last checkpoint, not the database.

This requires `HEADER_PAGE_ID` to become two reserved pages, and `AllocatePage` to refuse both.
Adjust doc 04's guard.

---

## 6. Measuring fragmentation

Doc 01 §5 and doc 04 §4 promised this. LIFO free-list reuse scrambles physical order, turning
sequential range scans into random I/O — same results, 15× slower, no error anywhere.

```cpp
// Fraction of leaf-chain steps that move to a physically adjacent page.
double DiskBPlusTree::ChainLocality() {
    page_id_t id = LeftmostLeafId();
    std::size_t steps = 0, sequential = 0;

    while (id != INVALID_PAGE_ID) {
        PageGuard g = m_Pool.FetchGuarded(id);
        const page_id_t next = g.AsNodeConst().NextLeaf();
        if (next != INVALID_PAGE_ID) {
            ++steps;
            if (next == id + 1) ++sequential;
        }
        id = next;
    }
    return steps ? static_cast<double>(sequential) / static_cast<double>(steps) : 1.0;
}
```

Measure it after a pure insert workload (expect near 1.0) and after heavy insert/delete churn
(expect it to collapse). Then time a full `RangeSearch` in both states. **That is your
fragmentation cost, in microseconds, on your hardware** — and the only honest basis for
deciding whether to build extent-based allocation or a compaction pass.

---

## 7. Concurrency — described, not built

This series is single-threaded, deliberately. The extension is worth understanding.

The technique is **latch crabbing**: hold a latch on the parent, take one on the child, and
release the parent *only once you know the child will not split or merge*. A child that is not
full (for insert) or above minimum (for delete) is "safe", because the operation cannot
propagate upward past it.

```
   descend:  latch root  ->  latch child  ->  child safe? -> release root
                                          ->  child unsafe? -> keep root latched
```

Two things you already have make this feasible:

- **Pin counts.** A latched page must be resident; the pin already guarantees it.
- **`PageGuard`.** Latch acquisition and release is the same RAII shape as pin/unpin — a
  `ReadPageGuard` / `WritePageGuard` split is the natural extension of doc 08.

The path stack becomes a **latch stack**, released from the bottom up. And this is where
preemptive splitting (doc 10 §2) becomes attractive after all: if you split every full node on
the way down, you never walk back up, so you can release each latch immediately and never hold
more than two. Concurrency changes which algorithm is better — which is a good note to end the
design on.

---

## 8. The crash test

This is the checkpoint that matters. Everything else in the series has been building to it.

```cpp
// storage/tests/crash_test.cpp -- run as: crash_test writer   /   crash_test verify
#include "../../datastructures/bplustree/DiskBPlusTree.hpp"
#include <cassert>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    const std::string path = "crash.db";
    const std::string mode = (argc > 1) ? argv[1] : "writer";

    if (mode == "verify") {
        DiskManager dm(path); BufferPool bp(dm, 256); DiskBPlusTree tree(dm, bp);

        // The ONLY requirement after a crash: the tree is structurally valid.
        assert(tree.Validate());

        // And everything up to the last checkpoint is present.
        std::size_t found = 0;
        for (int k = 0; k < 100000; ++k) if (tree.Contains(k)) ++found;
        std::cout << "valid after crash. " << found << " keys survived, height="
                  << tree.Height() << ", pages=" << dm.NumPages() << "\n";
        return 0;
    }

    std::remove(path.c_str());
    DiskManager dm(path); BufferPool bp(dm, 256); DiskBPlusTree tree(dm, bp);

    for (int k = 0; k < 100000; ++k) {
        tree.Insert(k, PostingRef{static_cast<std::uint64_t>(k), 1});
        if (k % 5000 == 0) {
            tree.Checkpoint();
            std::cout << "checkpoint at " << k << std::endl;   // flush stdout too
        }
    }
    tree.Checkpoint();
    std::cout << "done\n";
}
```

Then, from PowerShell, kill it partway and verify:

```powershell
$p = Start-Process .\crash_test -ArgumentList writer -PassThru -NoNewWindow
Start-Sleep -Milliseconds 300
Stop-Process -Id $p.Id -Force          # no cleanup, no destructors, no FlushAll
.\crash_test verify
```

**What must be true:** `Validate()` passes. The tree is a legal B+Tree.

**What need not be true:** that all 100,000 keys are there. Keys written after the last
checkpoint are gone, and that is the *documented* guarantee, not a failure. Losing
uncommitted work is correct; losing structural integrity is not.

If `Validate()` fails, work backwards: is it a broken leaf chain (§3 New #1), an unfreed page
(§3 New #2), or a header written before its data pages (§4)? Those three account for nearly
every failure at this stage.

---

## Checkpoint

- [ ] Delete-all in ascending, descending, and random order — `Validate()` after each
- [ ] Page accounting: after deleting everything, `NumPages() == FreePageCount() + reserved`.
      **This is your leak detector.** A tree that empties but does not return its pages will
      grow forever under churn.
- [ ] `bp.PinnedCount() == 0` after every operation
- [ ] Randomised insert/delete churn cross-checked against `std::set` — port the stress test
      from your in-memory suite; it works nearly unchanged, and your in-memory tree is the
      oracle
- [ ] **The crash test passing**, with `Validate()` true after a hard kill
- [ ] `ChainLocality()` measured before and after churn, with a `RangeSearch` timing for both
- [ ] An answer to: *why must data pages be flushed before the header?*
- [ ] An answer to: *why does a leaf merge have to touch three pages?*

Next: [12 — The Latency Lab](12-latency-lab.md), where you find out which of the last eleven
docs' decisions actually mattered.
