# 07 — BufferPool II: Eviction

> **Build target:** `LRUReplacer` plus a real `FindVictimFrame`. About 90 lines. At the end,
> a pool of 8 frames serves a file of 10,000 pages indefinitely, and you will measure the hit
> rate as a function of pool size and see the B+Tree's access pattern in the numbers.
>
> **The wall from doc 06, removed.** Two things have to happen and they are easy to conflate:
> a frame must become an *eviction candidate* when its pin count hits zero, and a candidate
> must be *chosen and cleaned* when a frame is needed. Candidacy and selection are separate
> mechanisms; keeping them separate is what makes this O(1).

---

## 1. What eviction has to guarantee

Before choosing a policy, be precise about correctness. A victim frame is legal only if:

1. **`pinCount == 0`.** Someone holding a `Page*` into that frame must never have it yanked.
   This is not a performance rule; violating it is a use-after-free.
2. **If dirty, it is written to disk before being overwritten.** Otherwise the modification is
   lost, silently, at an unpredictable time.
3. **It is removed from the page table before reuse.** Otherwise the table claims page 7 lives
   in frame 3 while frame 3 now holds page 99 — and `FetchPage(7)` returns page 99's bytes as
   a "hit". Silent wrong answer, no crash.

Everything else — LRU, Clock, random — is *policy*, and policy only affects speed. Those three
rules affect correctness. Get them right first and you can change the policy freely.

---

## 2. Choosing a policy

| Policy | Cost | Hit rate on a B+Tree | Notes |
|---|---|---|---|
| Random | O(1) | poor | Evicts the root as readily as a cold leaf |
| FIFO | O(1) | poor | The root is the *oldest* page — FIFO evicts exactly the wrong one |
| **LRU** | **O(1)** | **good** | Recency tracks the tree's shape almost perfectly |
| Clock | O(1), less memory | ≈ LRU | LRU approximation; what most real systems ship |
| LRU-K | O(log n) | best | Resists scan pollution; what BusTub asks for |

**FIFO deserves a moment**, because it shows why the access pattern matters more than the
algorithm's elegance. In a B+Tree the root is fetched first and touched by *every* subsequent
lookup. FIFO evicts in insertion order, so the root — the hottest page in the entire engine —
is first out the door. A policy can be O(1), simple, and catastrophically wrong for your
workload.

LRU gets this right for free: the root is touched constantly, so it is never the least recently
used. Same for the level below it. **The tree's shape and LRU's heuristic happen to agree**, and
that is why we use it.

> **Where LRU fails: sequential scans.** A `RangeSearch` over a million keys walks a million
> leaves, each touched exactly once, each evicting something useful. By the end your pool holds
> a million cold leaves and none of the internal nodes. This is **scan pollution**, and it is
> what LRU-K fixes by requiring *K* accesses before a page is considered worth keeping. Doc 11
> §6 measures whether it hurts you; build LRU now.

---

## 3. LRU in O(1)

The naïve implementation scans all frames for the oldest timestamp: O(n) per eviction. With a
few thousand frames on every miss, that shows up on a profile.

The standard structure is a **doubly-linked list plus a hash map into it**:

```
   front (most recently unpinned)                    back (least recently used)
      [ 4 ] <-> [ 1 ] <-> [ 7 ] <-> [ 2 ] <-> [ 0 ]
        ^                                       ^
        |                                       |
   just unpinned                          next victim

   m_Map : frame index -> iterator into that list      {4:it, 1:it, 7:it, 2:it, 0:it}
```

- **Unpin(frame):** `push_front`, store the iterator. O(1).
- **Pin(frame):** look up the iterator, `erase` it. O(1).
- **Victim():** take `back()`, erase from list and map. O(1).

This works because of a specific `std::list` guarantee: **iterators remain valid when other
elements are inserted or erased.** You could not do this with `std::vector` — every insert
would invalidate every stored iterator. Knowing which containers offer reference stability is
one of those small pieces of C++ knowledge that decides whether a design is possible.

> **C++ — `std::list`, and why it is usually the wrong container.** A doubly-linked list of
> individually heap-allocated nodes, each holding the value plus two pointers. That structure
> is what gives the guarantee above: a node never moves, so anything pointing at it stays
> valid.
>
> It is also why `std::list` is normally a poor default. Every element is a separate
> allocation, elements are scattered in memory, and traversing the list is a pointer chase with
> a cache miss per node. A `std::vector` beats it at linear scanning by an order of magnitude
> even for insertions in the middle, because `memmove` on contiguous memory is faster than
> chasing pointers. The usual advice — "use `list` when you insert in the middle a lot" — is
> wrong more often than it is right.
>
> **Here it is the correct choice**, for the one reason lists are ever correct: we need O(1)
> splice/erase *at a position we already hold an iterator to*, and we need that iterator to stay
> valid. Nothing else in the standard library offers both. We never traverse the list, so its
> cache behaviour is irrelevant — `Victim()` touches only `back()`, and `Pin` jumps straight to
> a node via the hash map.
>
> Note the memory cost is real: per frame you are paying a heap node (~32 bytes with the two
> pointers and allocator overhead) plus a hash map entry. For a 4096-frame pool that is a few
> hundred kilobytes of bookkeeping against 16 MB of pages — acceptable. At a million frames it
> would not be, which is one reason §7's Clock policy (one bit per frame, no list at all) is
> what production systems actually ship.
>
> `m_Map.reserve(numFrames)` in the constructor pre-allocates the hash table's buckets so it
> never rehashes during operation. The pool size is known up front, so there is no reason to
> let the map grow incrementally — the same "hoist the allocation out of the hot path"
> reasoning as doc 06 §4.3.

```cpp
#pragma once
// internal/kernal/core/storage/LRUReplacer.hpp
#include <cstddef>
#include <list>
#include <unordered_map>

// Tracks UNPINNED frames only. A frame in here is a legal eviction candidate; a frame absent
// from here is either pinned or already free.
class LRUReplacer {
public:
    explicit LRUReplacer(std::size_t numFrames) { m_Map.reserve(numFrames); }

    // Frame is now a candidate. Called when pinCount drops to 0.
    void Unpin(std::size_t frameIndex) {
        if (m_Map.count(frameIndex)) return;          // already a candidate; do not reorder
        m_List.push_front(frameIndex);
        m_Map[frameIndex] = m_List.begin();
    }

    // Frame is in use. Called when pinCount rises from 0, and on eviction.
    void Pin(std::size_t frameIndex) {
        auto it = m_Map.find(frameIndex);
        if (it == m_Map.end()) return;
        m_List.erase(it->second);
        m_Map.erase(it);
    }

    // Least recently unpinned candidate. False if there are none.
    bool Victim(std::size_t& outFrameIndex) {
        if (m_List.empty()) return false;
        outFrameIndex = m_List.back();
        m_List.pop_back();
        m_Map.erase(outFrameIndex);
        return true;
    }

    std::size_t Size() const { return m_List.size(); }

private:
    std::list<std::size_t>                                            m_List;
    std::unordered_map<std::size_t, std::list<std::size_t>::iterator> m_Map;
};
```

### Why `Unpin` returns early if already present

Without that guard, unpinning an already-unpinned frame would insert a second entry for it.
The map would point at the newer one, the older would be orphaned in the list, and `Victim`
would eventually hand out a frame index that is no longer a valid candidate — possibly one
that has since been pinned. Duplicate entries in an index structure are a classic source of
"impossible" bugs.

### Why `Pin` is a no-op when absent

`FetchPage` calls `Pin` on a hit without knowing whether the frame was a candidate. If it was
pinned already, it is not in the replacer, and that is fine. Making the operation idempotent
means callers do not have to track state they should not care about.

### Note what LRU is *not* tracking

Recency is recorded at **unpin**, not at fetch. A page pinned for a long time and then
released counts as recently used at the moment of release. That is the right semantic — it was
in use right up until then — and it is simpler than touching the list on every access.

---

## 4. The eviction path, in the only correct order

```cpp
std::size_t BufferPool::FindVictimFrame() {
    // 1. Prefer a genuinely free frame -- no flush, no page table churn.
    if (!m_FreeFrames.empty()) {
        const std::size_t idx = m_FreeFrames.back();
        m_FreeFrames.pop_back();
        return idx;
    }

    // 2. Otherwise evict.
    std::size_t idx;
    if (!m_Replacer.Victim(idx)) {
        throw std::runtime_error(
            "BufferPool: all " + std::to_string(m_PoolSize) +
            " frames are pinned. This is a leaked UnpinPage -- see doc 08.");
    }

    Frame& frame = m_Frames[idx];
    assert(frame.pinCount == 0 && "replacer handed out a pinned frame");

    // 3. Flush BEFORE anything else. If this throws, we have not yet corrupted the page
    //    table, and the frame is still consistently describing its old occupant.
    if (frame.dirty) {
        m_Disk.WritePage(frame.pageId, frame.page);
        ++m_Evictions;
        frame.dirty = false;
    }

    // 4. Only now break the old mapping. After this line, page frame.pageId is not resident.
    m_PageTable.erase(frame.pageId);

    // 5. Reset so a bug reads an obviously-invalid id rather than a plausible stale one.
    frame.pageId = INVALID_PAGE_ID;

    return idx;
}
```

### The ordering is the lesson

Try reordering steps 3 and 4 mentally. If you erase the page table entry first and the write
then throws — disk full, I/O error — the page is no longer findable *and* its only copy was in
a frame you are about to overwrite. You have destroyed data because of an error you were told
about.

**Do the fallible thing while you can still abandon the operation.** Mutate your bookkeeping
only once nothing can fail. This is the same discipline as strong exception safety, and it
applies to every multi-step state change in a storage engine.

### Why prefer free frames over eviction

A free frame costs nothing: no write, no page-table erase. An eviction may cost a 4096-byte
disk write. Early in the pool's life every frame is free, so the pool fills before it starts
evicting — which is exactly what you want.

### `FetchPage` and `UnpinPage` need two new lines

```cpp
// FetchPage, on the hit path:
    Frame& frame = m_Frames[it->second];
    if (frame.pinCount == 0) {
        m_Replacer.Pin(it->second);       // was a candidate; no longer
    }
    frame.pinCount++;

// UnpinPage, at the end:
    frame.pinCount--;
    if (frame.pinCount == 0) {
        m_Replacer.Unpin(it->second);     // now a candidate
    }
```

Both are guarded on the transition to and from zero, not called unconditionally. Calling
`Unpin` while the count is still positive would make a live page evictable — rule 1 violated,
use-after-free available.

`DeletePage` also needs `m_Replacer.Pin(idx)` before returning the frame to the free list: a
frame must not be in both the replacer and the free list, or it will be handed out twice.

---

## 5. What this changes about the wall

Rerun doc 06's wall test. A pool of 8 frames now serves any number of pages, provided you
unpin. The error message changes meaning entirely: it no longer says "the pool is small", it
says **"you leaked an unpin"**, which is a bug in the caller.

That reframing is worth pausing on. After this doc, `FindVictimFrame` throwing is *always* a
caller bug, never a capacity problem. Which makes it a diagnostic — and makes doc 08's RAII
guard the fix rather than a nicety.

---

## 6. Measuring — the part that makes this real

Add counters and measure hit rate against pool size. This is the experiment that turns the
buffer pool from a component you built into a component you understand.

```cpp
// storage/tests/bufferpool_hitrate_lab.cpp
#include "../BufferPool.hpp"
#include "../NodePage.hpp"
#include <iostream>
#include <random>
#include <iomanip>

int main() {
    constexpr int TOTAL_PAGES = 10000;
    std::remove("hitrate.db");

    {   // build a file of 10,000 pages
        DiskManager dm("hitrate.db");
        BufferPool  bp(dm, 256);
        for (int i = 0; i < TOTAL_PAGES; ++i) {
            page_id_t id;
            Page* p = bp.NewPage(id);
            NodePage(*p).Init(NodeType::Leaf);
            bp.UnpinPage(id, true);
        }
        bp.FlushAll();
    }

    std::cout << std::fixed << std::setprecision(1);
    std::cout << "pool | uniform random | zipf-ish (tree-like) | sequential scan\n";

    for (std::size_t pool : {16u, 64u, 256u, 1024u, 4096u}) {
        double rates[3];

        for (int mode = 0; mode < 3; ++mode) {
            DiskManager dm("hitrate.db");
            BufferPool  bp(dm, pool);
            std::mt19937 rng(42);

            for (int i = 0; i < 200000; ++i) {
                page_id_t id;
                if (mode == 0) {
                    id = 1 + rng() % TOTAL_PAGES;                 // uniform
                } else if (mode == 1) {
                    // Crude skew: most accesses hit a small hot set, like a tree's upper
                    // levels. This is the shape a real B+Tree workload has.
                    id = (rng() % 100 < 90) ? 1 + rng() % 50
                                            : 1 + rng() % TOTAL_PAGES;
                } else {
                    id = 1 + (i % TOTAL_PAGES);                   // sequential scan
                }
                bp.FetchPage(id);
                bp.UnpinPage(id, false);
            }
            rates[mode] = bp.HitRate() * 100;
        }
        std::cout << std::setw(4) << pool << " | "
                  << std::setw(13) << rates[0] << "% | "
                  << std::setw(20) << rates[1] << "% | "
                  << std::setw(14) << rates[2] << "%\n";
    }
}
```

### What you should see, and what each column teaches

- **Uniform random** tracks `pool / TOTAL_PAGES` almost exactly. With no locality, a cache can
  only hold the fraction of the working set that fits. **This is the floor**: no policy beats
  it, because there is no pattern to exploit.
- **Skewed** hits 90%+ even at pool size 64, because the hot set fits. This is the real B+Tree
  case, and it is why a small pool works: **the upper levels of the tree are a tiny, permanently
  hot working set.** Root plus level 2 for a 4-level tree is a handful of pages.
- **Sequential scan** is near 0% until the pool holds the entire file, then jumps to ~100%.
  This is scan pollution in one column: LRU's heuristic is exactly wrong here, because the
  least-recently-used page is the one you are about to need again on the next pass.

Write your numbers down. Doc 11 §6 asks whether your real tree workload looks like column 2
(good) or column 3 (a reason to consider LRU-K).

---

## 7. Two policies worth knowing

**Clock** (second-chance) approximates LRU with one bit per frame and no list. A hand sweeps
the frames; if the reference bit is set it clears it and moves on, otherwise it evicts. It gets
most of LRU's benefit at a fraction of the memory and with far less pointer chasing, which is
why PostgreSQL and most operating systems use a variant of it. If you ever profile the LRU
list as hot, this is the replacement.

**LRU-K** tracks the last K access times and evicts by the Kth-most-recent. With K=2, a page
touched once (a scan) is evicted before a page touched twice (a real hot page). It directly
solves column 3 above, at the cost of a priority queue instead of a list.

Neither changes the three correctness rules from §1. That is the payoff of separating policy
from mechanism: swapping the replacer is a contained change.

---

## Checkpoint

Extend your test:

```cpp
// eviction actually recycles frames
DiskManager dm("bp_evict.db");
BufferPool  bp(dm, 4);

std::vector<page_id_t> ids;
for (int i = 0; i < 4; ++i) {
    page_id_t id; Page* p = bp.NewPage(id);
    NodePage n(*p); n.Init(NodeType::Leaf); n.SetKeyCount(1);
    n.SetKeyAt(0, static_cast<disk_key_t>(i) + 100);
    ids.push_back(id);
    bp.UnpinPage(id, true);                       // all four are now candidates
}

// A fifth page must evict the least recently unpinned (ids[0]) rather than throwing.
page_id_t fifth; bp.NewPage(fifth); bp.UnpinPage(fifth, true);

// ids[0] was evicted -- fetching it must be a MISS that reads from disk...
const auto before = dm.ReadCount();
Page* revived = bp.FetchPage(ids[0]);
assert(dm.ReadCount() == before + 1);
// ...and it must come back with the data intact, proving the dirty flush on eviction worked.
assert(NodePage(*revived).KeyAt(0) == 100);
bp.UnpinPage(ids[0], false);

// All pinned -> the error now means "leaked unpin", not "pool too small"
for (int i = 0; i < 4; ++i) { page_id_t id; bp.NewPage(id); }   // 4 pinned, pool is 4
bool threw = false;
try { page_id_t id; bp.NewPage(id); } catch (const std::exception&) { threw = true; }
assert(threw);
```

Before doc 08, you should have:

- [ ] A 4-frame pool serving far more than 4 pages
- [ ] **The dirty-eviction round trip proven**: modify, let it be evicted, fetch again, data
      intact. If this fails, step 3 of §4 is wrong and you are losing writes.
- [ ] The hit-rate table from §6 filled in with your numbers
- [ ] An answer to: *why must the flush happen before the page-table erase?*
- [ ] An answer to: *why is FIFO a bad policy specifically for a B+Tree?*
- [ ] An answer to: *what does "all frames pinned" now tell you about your code?*

Next: [08 — PageGuard](08-page-guards.md), which makes the leaked unpin impossible rather than
merely detectable.
