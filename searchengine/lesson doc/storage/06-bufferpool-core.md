# 06 — BufferPool I: Frames

> **Build target:** `internal/kernal/core/storage/BufferPool.hpp` / `.cpp` — a fixed pool of
> in-memory frames, a page table, pin counts, and dirty flags. About 200 lines. At the end,
> fetching the same page twice performs **one** disk read, and you will prove it with the
> counters you added in doc 03.
>
> **Deliberately incomplete.** This doc builds a pool that works until it runs out of frames
> and then throws. That is not an oversight — it is so you feel the wall before doc 07 builds
> eviction. A cache with an eviction policy you did not need is a cache you do not understand.

---

## 1. Why we cache at all, in numbers you measured

From doc 01 §2, on your own machine:

```
  hash lookup + pointer return              ~20 ns
  syscall + OS page cache hit            ~1,000 ns      50x worse
  syscall + actual NVMe read            ~20,000 ns    1000x worse
```

The OS *already* caches file pages. So why build another cache on top?

**Because a cache you have to make a syscall to reach is barely a cache.** Even on a perfect
OS page-cache hit you pay the mode switch, the copy from kernel memory into your `Page`, and
the TLB pressure. Our hit path does none of that: a hash lookup, and we hand back a pointer to
memory the caller can read directly.

There are three further reasons, and each becomes load-bearing later:

1. **Pinning.** The tree needs to say "I am using page 42, do not let it disappear." The OS
   page cache offers no such guarantee — it will evict whatever it likes, whenever it likes.
   Without pinning, doc 10's split cannot safely hold a parent and two children at once.
2. **Dirty tracking.** We know precisely which pages changed and can write exactly those.
3. **We control the policy.** A B+Tree has a strongly skewed access pattern — the root is
   touched by *every* lookup, internal nodes often, leaves rarely twice. A general-purpose OS
   policy cannot know that. Doc 07 exploits it.

---

## 2. The shape

```
   BufferPool
   +--------------------------------------------------------------+
   |  m_Frames : Frame[POOL_SIZE]     <- allocated ONCE, never moves |
   |                                                                |
   |    [0] page 7   pin=1 dirty      [1] page 3  pin=0             |
   |    [2] -empty-                   [3] page 9  pin=2 dirty       |
   |                                                                |
   |  m_PageTable : page_id -> frame index      {7:0, 3:1, 9:3}     |
   |  m_FreeFrames : stack of unused indices    [2, 4, 5, ...]      |
   +--------------------------------------------------------------+
                              |
                              v
                        DiskManager
```

Three structures, each answering one question:

- **`m_Frames`** — the actual memory. Fixed size, decided at construction.
- **`m_PageTable`** — "is page N resident, and if so, where?" This is the hit test.
- **`m_FreeFrames`** — "where can I put a new page?" Without it, finding an empty frame is an
  O(POOL_SIZE) scan on every miss.

---

## 3. The Frame

```cpp
struct Frame {
    Page      page;                        // the 4096 bytes; doc 02 kept this pure

    page_id_t pageId   = INVALID_PAGE_ID;  // which page is resident here, if any
    int       pinCount = 0;                // how many callers currently hold it
    bool      dirty    = false;            // has it been modified since it was read?
};
```

Doc 02 §3 argued for this split; here is where it pays off. `WritePage(id, frame.page)` writes
exactly 4096 bytes because `Page` is exactly 4096 bytes. The bookkeeping lives beside the data,
never inside it, and cannot leak onto the disk.

> **C++ — default member initialisers.** `page_id_t pageId = INVALID_PAGE_ID;` inside the
> struct is a **default member initialiser** (C++11). It applies to every constructor that does
> not explicitly initialise that member, so there is one place to state the default rather than
> one per constructor.
>
> This is the cheapest bug prevention in the class. Without it, `Frame` is a POD whose members
> hold whatever was on the heap: a garbage `pinCount` makes the frame permanently unevictable,
> a garbage `pageId` collides with a real page and `FetchPage` returns the wrong data as a
> "hit". Both are silent.
>
> Note the interaction with §4.1: `std::make_unique<Frame[]>(n)` value-initialises, which runs
> these initialisers for all N frames. Had `Frame` used `Page page;` with no initialiser and no
> other members, `make_unique` would still zero it — but the moment you add one member with an
> initialiser, the type stops being trivially default-constructible and the rules change. Do
> not rely on that subtlety; **write the initialiser you need.**
>
> Related, and worth distinguishing: `FileHeader m_Header{};` in doc 04 uses **empty braces**,
> which value-initialises — zeroing everything without a default member initialiser.
> `FileHeader m_Header;` (no braces) would leave it indeterminate. One pair of braces is the
> difference between a zeroed header and stack garbage written to page 0.

### `pinCount` — the whole safety model

A pinned page **may not be evicted**. That is the entire contract, and everything in doc 07
respects it.

Why it must be a count and not a flag: during a split, the tree holds the parent *and* the
node being split *and* the new sibling. During descent it may briefly hold two levels. If two
independent parts of the code fetch the same page, a boolean would let the first `Unpin` free
a page the second is still reading. A count handles nesting correctly.

**The invariant to burn in: every `FetchPage` must be matched by exactly one `UnpinPage`.**
Miss one and that frame is permanently unusable. Miss enough and the pool has no evictable
frames and the engine stops. Doc 08 makes this structurally impossible with RAII — but build
it manually first so you understand what the guard is guarding.

### `dirty` — an optimisation with teeth

If a page was only read, its disk copy is already correct and eviction can discard it for free.
If it was modified, eviction must write it first.

The failure mode is asymmetric and worth stating plainly:

- **Wrongly marked dirty:** a pointless 4096-byte write. Slow. Harmless.
- **Wrongly marked clean:** the modification is **silently discarded** on eviction. Your
  insert appeared to work, `Validate()` passed while the page was resident, and the data
  vanished at some unpredictable later moment.

That asymmetry is why `UnpinPage` takes `isDirty` as an explicit argument rather than trying
to detect changes. Guessing is not available; the caller must declare. When in doubt, declare
dirty.

---

## 4. Why the frame array must never reallocate

```cpp
// In the constructor -- allocated once, for the lifetime of the pool.
m_Frames = std::make_unique<Frame[]>(m_PoolSize);
```

**Not `std::vector<Frame>` with `push_back`.** This is the most important line in the doc.

`FetchPage` returns `Page*` — a pointer into a frame. If the frame storage ever reallocated,
every outstanding pointer would dangle instantly. The tree would be reading freed memory, and
the bug would be non-deterministic: fine until the vector happened to grow, then corruption
whose cause is thousands of operations upstream.

So the frame array is allocated once and never grows. That is not a limitation, it is the
definition — **a buffer pool is a fixed amount of memory.** Its whole purpose is to bound
memory use while serving an unbounded file. A pool that grows on demand is just a leak with
extra steps.

> `std::vector` would actually be fine here *provided* you `reserve()` once and never insert
> again. The reason to use `unique_ptr<Frame[]>` anyway is that it makes the guarantee
> structural rather than a comment someone deletes. When correctness depends on "nobody ever
> calls `push_back`", pick a type with no `push_back`.

### 4.1 `std::unique_ptr<T[]>` — the array form

```cpp
std::unique_ptr<Frame[]> m_Frames = std::make_unique<Frame[]>(m_PoolSize);
```

A **smart pointer**: it owns a heap allocation and frees it in its destructor. The `[]` in the
template argument is not decoration — it selects a partial specialisation that differs in two
ways:

1. **It calls `delete[]`, not `delete`.** Mixing these is undefined behaviour, and it is a
   real bug: `delete` on an array-new pointer runs one destructor instead of N and hands the
   allocator the wrong size. The `[]` in the type makes it impossible to get wrong.
2. **It provides `operator[]` and not `operator*`/`operator->`.** The API matches what an
   array is.

`std::make_unique<Frame[]>(n)` **value-initialises** all N elements — every `Frame` gets its
default member initialisers (`pageId = INVALID_PAGE_ID`, `pinCount = 0`, `dirty = false`). That
matters: an uninitialised `pinCount` of garbage would make a frame permanently unevictable, and
an uninitialised `pageId` would collide with a real page. If you ever need to skip that cost,
C++20's `std::make_unique_for_overwrite` leaves them uninitialised — do not use it here.

What it buys over a raw `new Frame[n]`: the destructor, on every path including exceptions. What
it costs: nothing. `sizeof(unique_ptr<T[]>)` is one pointer, and every operation inlines to the
same code you would have written by hand. **This is the model for zero-overhead abstraction in
C++, and doc 08's `PageGuard` is the same idea applied to a pin instead of a heap block.**

### 4.2 Which containers can move your data under you

The rule that decided this design generalises, and it is worth memorising because it silently
governs a great deal of C++:

| Container | Pointers/references to elements survive… |
|---|---|
| `std::vector` | …nothing that grows it. `push_back` past `capacity()` **reallocates and invalidates everything** |
| `std::deque` | …`push_back`/`push_front` (references stay valid; *iterators* do not) |
| `std::list` | …**everything** except erasing that element |
| `std::unordered_map` | …**everything** except erasing that element (rehash invalidates *iterators*, not references) |
| `std::array` / C array | …everything; it never moves |

`std::vector` is the odd one out and the one people reach for by default. Its contiguity — the
reason it is fast — is exactly what forces reallocation on growth.

Two places in this series depend on a row of that table:

- **Here**: `FetchPage` returns `Page*` into a frame, so frames must never move.
- **Doc 07**: `LRUReplacer` stores `std::list::iterator`s in a hash map. That only works because
  list iterators survive insertions and erasures of *other* elements. The same design over a
  `vector` would be broken on the first `push_back`.

**Whenever you store a pointer, reference, or iterator into a container, check that row.** It
is one of the highest-yield habits in the language.

### 4.3 Arenas — what you have just built without naming it

`m_Frames` is an **arena**: one large allocation, carved into fixed-size slots, managed by your
own free list (`m_FreeFrames`), released all at once when the pool dies.

The general idea is that `new`/`delete` per object is expensive and unpredictable — a
general-purpose allocator must search free lists, may take a lock, may fragment, and may fault
into cold memory. An arena replaces all of it with:

```
   allocate:  pop an index off a free list        (a few instructions)
   free:      push the index back                 (a few instructions)
   destroy:   free the whole block, once          (one call)
```

Why this fits a buffer pool perfectly:

- **All objects are the same size**, so there is no fragmentation and no size-class search.
- **The count is fixed and known up front** — that *is* the definition of a buffer pool.
- **Lifetimes are uniform**: every frame dies with the pool.
- **Locality.** All 4096 frames are contiguous, so the page table's hot entries and the frames
  they point at share TLB coverage. Scattered `new Frame` allocations would not.

Variants you will meet elsewhere, all the same family:

- **Bump allocator** — a pointer that only moves forward; "free" is a no-op and you reset the
  whole arena at once. Used for per-request or per-frame allocations in servers and game
  engines. Fastest possible allocation: one add.
- **Pool / slab allocator** — exactly what you built: fixed-size slots plus a free list. Linux's
  slab allocator is this idea for kernel objects.
- **Monotonic buffer resource** — the standard library's version, `std::pmr::monotonic_buffer_resource`,
  which lets ordinary containers allocate from an arena you supply.

> **Where an arena would help in this codebase.** Doc 09's `SearchPath` uses
> `std::vector<page_id_t>`, allocating and freeing on **every insert**. That is the classic
> case for arena-style thinking, and doc 12 §6 measures replacing it with a fixed
> `std::array` — which is the degenerate arena: storage with no allocator at all, on the stack.
> The measured win there is not the allocation's *average* cost, it is its **variance**: a
> `malloc` can take a lock or fault, and that shows up in p99.
>
> The general rule: **an allocation in a hot path is a latency risk, not just a cost.** When
> the size and count are known, hoist the allocation out and hand out slots instead.

Sizing: the pool is `POOL_SIZE × 4096` bytes. 1024 frames = 4 MB; 262144 frames = 1 GB. Doc 07
§6 measures hit rate against pool size, which is the only honest way to choose.

---

## 5. `FetchPage` — the core operation

```cpp
Page* BufferPool::FetchPage(page_id_t pageId) {
    if (pageId == INVALID_PAGE_ID) {
        throw std::runtime_error("FetchPage: INVALID_PAGE_ID");
    }

    // ---- 1. Hit? -------------------------------------------------------------
    auto it = m_PageTable.find(pageId);
    if (it != m_PageTable.end()) {
        Frame& frame = m_Frames[it->second];
        frame.pinCount++;
        ++m_Hits;
        // Doc 07 adds: m_Replacer.Pin(it->second) -- a pinned frame is not a candidate.
        return &frame.page;
    }
    ++m_Misses;

    // ---- 2. Miss: find somewhere to put it ------------------------------------
    const std::size_t frameIndex = FindVictimFrame();   // doc 07 makes this interesting

    Frame& frame = m_Frames[frameIndex];

    // ---- 3. Read it in --------------------------------------------------------
    m_Disk.ReadPage(pageId, frame.page);

    frame.pageId   = pageId;
    frame.pinCount = 1;          // the caller holds it; they owe us exactly one Unpin
    frame.dirty    = false;      // fresh from disk, therefore identical to disk

    m_PageTable[pageId] = frameIndex;
    return &frame.page;
}
```

### Why the hit path is first and short

It is the common case — a decent pool hits 95%+ on a B+Tree workload, because the upper levels
of the tree are touched by every single lookup. Everything on that path costs latency on every
operation. One hash lookup, one increment, one pointer return.

> **C++ — `std::unordered_map` and the `find`/`end` idiom.** A hash table: **O(1) average**
> lookup, O(n) worst case if every key collides. The ordered `std::map` is a red-black tree at
> O(log n) and would be the wrong choice here — we never iterate the page table in key order,
> so we would be paying for a guarantee we do not use.
>
> ```cpp
> auto it = m_PageTable.find(pageId);
> if (it != m_PageTable.end()) { ... it->second ... }
> ```
>
> `find` returns an **iterator**, and `end()` is the one-past-the-last sentinel meaning "not
> found". `it->first` is the key, `it->second` the value — the element is a `std::pair`.
>
> Two mistakes to avoid, both of which look harmless:
>
> - **`m_PageTable[pageId]`** on a missing key **inserts** a default-constructed value and
>   returns a reference to it. It is not a lookup; it is a lookup-or-create, and it cannot be
>   used on a `const` map. Using it to test membership silently grows your page table with
>   bogus frame indices — pointing at frame 0.
> - **`count(k)` then `[k]`** hashes the key twice. `find` once and reuse the iterator.
>
> One caveat that matters given §4.2: **rehashing invalidates iterators but not references or
> pointers to elements.** So caching an iterator across an insert is unsafe; caching a
> `Frame*` is fine — but note we store frame *indices*, not pointers, which sidesteps the
> question entirely. Indices into a fixed array are the most robust handle available: they
> survive anything the container does.

### Why `pinCount = 1` and not `++` on the miss path

The frame was just claimed for this page; whatever count it had belonged to its previous
occupant and must not carry over. If it were non-zero we would not have been allowed to evict
it in the first place — which is a real invariant, and worth an `assert(frame.pinCount == 0)`
inside `FindVictimFrame` rather than a comment here.

### Why `dirty = false` on the miss path

We just read from disk, so memory and disk agree by construction. Note the ordering
subtlety: `FindVictimFrame` is responsible for flushing the *previous* occupant if it was
dirty, **before** we overwrite the frame. Get that order wrong and you lose a page's worth of
writes every time the pool is full. Doc 07 §4 is exactly this.

---

## 6. `NewPage` — allocation without a disk round trip

```cpp
Page* BufferPool::NewPage(page_id_t& outPageId) {
    const page_id_t pageId = m_Disk.AllocatePage();
    const std::size_t frameIndex = FindVictimFrame();

    Frame& frame = m_Frames[frameIndex];

    // Do NOT read from disk. The page is logically blank; reading it would be a guaranteed
    // cache miss returning zeros we can produce for free. This is the optimisation doc 04
    // section 4 promised.
    std::memset(frame.page.data, 0, PAGE_SIZE);

    frame.pageId   = pageId;
    frame.pinCount = 1;
    frame.dirty    = true;       // it exists only in memory; it MUST reach disk eventually

    m_PageTable[pageId] = frameIndex;
    outPageId = pageId;
    return &frame.page;
}
```

Two decisions worth naming.

**No read on allocate.** A newly allocated page contains nothing. Reading it from disk costs a
full I/O to obtain zeros. `memset` produces the same bytes at memory bandwidth. On a
split-heavy insert workload this removes roughly one disk read per split.

**`dirty = true` from birth.** The page exists only in memory. If it were marked clean,
eviction would discard it and the page id would point at stale bytes on disk — a node that the
tree believes exists and that contains someone else's data. This is the single most dangerous
flag in the class; get it right here and it stays right.

**The signature returns the id through an out-parameter** because the function already returns
the pointer, and the caller genuinely needs both. `std::pair` would be tidier and slightly
worse at the call site. Reasonable people differ; be consistent.

---

## 7. `UnpinPage` — where the accounting is settled

```cpp
bool BufferPool::UnpinPage(page_id_t pageId, bool isDirty) {
    auto it = m_PageTable.find(pageId);
    if (it == m_PageTable.end()) {
        return false;               // not resident: a bug in the caller, not a valid state
    }

    Frame& frame = m_Frames[it->second];

    if (frame.pinCount <= 0) {
        throw std::runtime_error("UnpinPage: page " + std::to_string(pageId)
                                 + " was not pinned -- double unpin");
    }

    // Sticky OR, never assignment. If ANY holder modified the page, it is dirty, and a
    // later read-only holder unpinning with false must not erase that fact.
    frame.dirty = frame.dirty || isDirty;

    frame.pinCount--;

    if (frame.pinCount == 0) {
        // Doc 07 adds: m_Replacer.Unpin(it->second) -- now a candidate for eviction.
    }
    return true;
}
```

### `frame.dirty |= isDirty` is not a style choice

Consider: A fetches page 7 and writes to it. B fetches page 7 to read. B unpins with
`isDirty = false`. If that were an assignment, A's modification is now marked clean, and the
next eviction throws it away.

**Dirtiness is monotonic until the page is written to disk.** Only `FlushPage` may clear it.
Encode that in the operator you choose.

### Why a double unpin throws rather than returning false

A double unpin means the caller's accounting is broken — some other holder's pin has just been
released out from under them, and that page can now be evicted while in use. The resulting
corruption would appear far away and much later. Failing loudly at the exact site of the
accounting error is worth an exception. This class of bug is the entire reason doc 08 exists.

---

## 8. Flushing, and the "no eviction yet" wall

```cpp
bool BufferPool::FlushPage(page_id_t pageId) {
    auto it = m_PageTable.find(pageId);
    if (it == m_PageTable.end()) return false;

    Frame& frame = m_Frames[it->second];
    if (frame.dirty) {
        m_Disk.WritePage(pageId, frame.page);
        frame.dirty = false;          // memory and disk now agree
    }
    return true;
}

void BufferPool::FlushAll() {
    for (std::size_t i = 0; i < m_PoolSize; ++i) {
        if (m_Frames[i].pageId != INVALID_PAGE_ID && m_Frames[i].dirty) {
            m_Disk.WritePage(m_Frames[i].pageId, m_Frames[i].page);
            m_Frames[i].dirty = false;
        }
    }
}
```

Note `FlushPage` does **not** unpin, and does not require the page to be unpinned. Flushing is
about disk consistency; pinning is about memory residency. Two orthogonal concerns that get
conflated constantly.

And now the deliberate wall:

```cpp
std::size_t BufferPool::FindVictimFrame() {
    if (!m_FreeFrames.empty()) {
        const std::size_t idx = m_FreeFrames.back();
        m_FreeFrames.pop_back();
        return idx;
    }

    // Doc 07 replaces this with LRU eviction.
    throw std::runtime_error(
        "BufferPool: out of frames. Either every page is pinned (a leaked Unpin -- see doc 08) "
        "or you need eviction (doc 07).");
}
```

**Run into this on purpose.** Make a pool of 8 frames, fetch 9 distinct pages, and watch it
throw. Then unpin one and fetch again, and watch it *still* throw — because a free frame and
an unpinned frame are not the same thing, and nothing yet moves a frame from the second
category to the first. That gap is precisely what doc 07 fills, and feeling it is worth more
than reading about it.

---

## 9. The header

```cpp
#pragma once
// internal/kernal/core/storage/BufferPool.hpp
#include <cstddef>
#include <memory>
#include <unordered_map>
#include <vector>
#include "DiskManager.hpp"
#include "Page.hpp"

struct Frame {
    Page      page;
    page_id_t pageId   = INVALID_PAGE_ID;
    int       pinCount = 0;
    bool      dirty    = false;
};

class BufferPool {
public:
    BufferPool(DiskManager& disk, std::size_t poolSize);
    ~BufferPool();

    BufferPool(const BufferPool&)            = delete;
    BufferPool& operator=(const BufferPool&) = delete;

    Page* FetchPage(page_id_t pageId);                  // pins; caller MUST unpin
    Page* NewPage(page_id_t& outPageId);                // allocates + pins
    bool  UnpinPage(page_id_t pageId, bool isDirty);
    bool  FlushPage(page_id_t pageId);
    void  FlushAll();
    bool  DeletePage(page_id_t pageId);                 // free it on disk too

    std::size_t   PoolSize()   const { return m_PoolSize; }
    std::uint64_t Hits()       const { return m_Hits;   }
    std::uint64_t Misses()     const { return m_Misses; }
    double        HitRate()    const {
        const std::uint64_t total = m_Hits + m_Misses;
        return total ? static_cast<double>(m_Hits) / static_cast<double>(total) : 0.0;
    }
    std::size_t   PinnedCount() const;                  // for tests and leak hunting

private:
    std::size_t FindVictimFrame();

    DiskManager&                                m_Disk;
    std::size_t                                 m_PoolSize;
    std::unique_ptr<Frame[]>                    m_Frames;      // never reallocated -- see 4
    std::unordered_map<page_id_t, std::size_t>  m_PageTable;
    std::vector<std::size_t>                    m_FreeFrames;

    std::uint64_t m_Hits   = 0;
    std::uint64_t m_Misses = 0;
};
```

`DeletePage` is worth writing yourself. It must: refuse if the page is pinned (someone is
using it), remove it from the page table, return the frame to the free list, reset the frame's
metadata, and call `m_Disk.DeallocatePage`. Order matters — deallocate on disk *last*, so that
a throw partway through does not leave the page on the free list while still in the page table.

The destructor calls `FlushAll()` inside a `try/catch`. A destructor that throws during stack
unwinding calls `std::terminate` and you lose the original exception — and with it, any chance
of diagnosing what actually went wrong.

---

## Checkpoint

`storage/tests/bufferpool_core_test.cpp`:

```cpp
#include "../BufferPool.hpp"
#include "../NodePage.hpp"
#include <cassert>
#include <iostream>

int main() {
    std::remove("bp_core.db");
    DiskManager dm("bp_core.db");
    BufferPool  bp(dm, 8);

    // ---- NewPage does not read from disk ----
    const auto readsBefore = dm.ReadCount();
    page_id_t id0;
    Page* p0 = bp.NewPage(id0);
    assert(dm.ReadCount() == readsBefore);          // zero reads for a brand-new page

    NodePage n0(*p0);
    n0.Init(NodeType::Leaf);
    n0.SetKeyCount(1);
    n0.SetKeyAt(0, 12345);
    assert(bp.UnpinPage(id0, true));                // dirty: we modified it

    // ---- second fetch of the same page is a HIT: no disk read ----
    const auto readsBeforeHit = dm.ReadCount();
    Page* again = bp.FetchPage(id0);
    assert(dm.ReadCount() == readsBeforeHit);       // still cached
    assert(NodePage(*again).KeyAt(0) == 12345);
    assert(bp.UnpinPage(id0, false));

    // ---- dirty is sticky: the read-only unpin above must not have cleared it ----
    bp.FlushPage(id0);
    {
        Page verify;
        dm.ReadPage(id0, verify);
        assert(NodePage(verify).KeyAt(0) == 12345); // survived, so it was still dirty
    }

    // ---- pin counts nest ----
    bp.FetchPage(id0);
    bp.FetchPage(id0);
    assert(bp.PinnedCount() == 1);                  // one FRAME pinned, count 2
    bp.UnpinPage(id0, false);
    assert(bp.PinnedCount() == 1);                  // still pinned once
    bp.UnpinPage(id0, false);
    assert(bp.PinnedCount() == 0);

    // ---- double unpin is a caller bug and must be loud ----
    bool threw = false;
    try { bp.UnpinPage(id0, false); } catch (const std::exception&) { threw = true; }
    assert(threw);

    // ---- THE WALL: 8 frames, 9 pages, no eviction yet ----
    std::vector<page_id_t> ids;
    threw = false;
    try {
        for (int i = 0; i < 20; ++i) { page_id_t id; bp.NewPage(id); ids.push_back(id); }
    } catch (const std::exception& e) {
        threw = true;
        std::cout << "hit the wall as expected: " << e.what() << "\n";
    }
    assert(threw);

    // ---- and unpinning does NOT help, because nothing recycles frames yet ----
    for (page_id_t id : ids) bp.UnpinPage(id, true);
    threw = false;
    try { page_id_t id; bp.NewPage(id); } catch (const std::exception&) { threw = true; }
    assert(threw);
    std::cout << "unpinned frames are still not free -- that gap is doc 07\n";

    std::cout << "bufferpool_core_test OK  (hit rate "
              << bp.HitRate() * 100 << "%)\n";
}
```

Before doc 07, you should have:

- [ ] A hit performing zero disk reads, proven with `dm.ReadCount()`
- [ ] `NewPage` performing zero disk reads
- [ ] Dirty-stickiness proven (a read-only unpin not erasing a prior write)
- [ ] The wall hit deliberately, **and** the follow-up proving unpinning doesn't help
- [ ] An answer to: *why is `m_Frames` a `unique_ptr<Frame[]>` and not a `vector` you
      `push_back` into?*
- [ ] An answer to: *what exactly goes wrong if a page is wrongly marked clean?*

Next: [07 — BufferPool II: Eviction](07-bufferpool-eviction.md), which turns that wall into a
policy.
