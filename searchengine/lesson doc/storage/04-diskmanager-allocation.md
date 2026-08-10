# 04 — DiskManager II: Allocation

> **Build target:** page 0 becomes a real header, and `AllocatePage()` / `DeallocatePage()`
> replace hardcoded page numbers. About 120 more lines. At the end, a page you free is handed
> back out by the next allocation, and the file stops growing forever.
>
> **The idea that makes this doc worth reading:** the list of free pages is stored **inside the
> free pages themselves**, costing exactly zero extra bytes. That trick — using dead space to
> track dead space — shows up everywhere in systems programming, and once you have written it
> once you will see it in every allocator you ever read.

---

## 1. The problem

Doc 03 gave you `WritePage(3, page)`. Where did the 3 come from? You typed it. That does not
scale past a test.

Three questions need answering, and none of them can be answered without state that outlives
the process:

1. **Which page do I use next?** Requires knowing how many exist.
2. **What happens to a page after a merge frees it?** Without reuse, a workload that inserts
   and deletes forever grows the file forever, even at constant key count.
3. **After reopening the file, where is the root of my tree?** Page ids are just integers; the
   root's id is not deducible from anything. Lose it and the entire tree is unreachable —
   every byte still on disk, and no way in.

All three are solved by reserving one page to describe the file. That is page 0.

---

## 2. The header page

```cpp
// internal/kernal/core/storage/FileHeader.hpp
#pragma once
#include <cstdint>
#include "Page.hpp"

// Lives at byte 0 of the file, inside page 0. Everything else in the file is reachable from
// here -- which is exactly why section 7 treats writing it as the engine's commit point.
struct FileHeader {
    std::uint32_t magic;          // "SEDB" -- is this even our file?
    std::uint32_t version;        // format version; refuse to open a future one
    std::uint32_t pageSize;       // must equal PAGE_SIZE; catches a rebuild with a new size
    std::uint32_t numPages;       // total pages ever allocated, including page 0
    page_id_t     freeListHead;   // first free page, or INVALID_PAGE_ID
    page_id_t     rootPageId;     // B+Tree root; INVALID_PAGE_ID when the tree is empty
    std::uint32_t numFreePages;   // bookkeeping only -- lets Validate() check the list length
    std::uint32_t reserved;       // pad to 32 bytes; see below
};

static_assert(sizeof(FileHeader) == 32, "FileHeader layout must be stable across builds");
static_assert(sizeof(FileHeader) <= PAGE_SIZE, "header must fit in page 0");

inline constexpr std::uint32_t SEDB_MAGIC   = 0x42444553u;   // 'S','E','D','B' little-endian
inline constexpr std::uint32_t SEDB_VERSION = 1u;
```

> **C++ — struct layout and padding.** Doc 02 §2.2 rejected a field-based `Page` partly
> because of padding. Here is the mechanism, because `FileHeader` is the first struct in the
> series whose size you actually assert.
>
> The compiler lays members out **in declaration order**, but each member must land on an
> address that is a multiple of its own alignment (doc 02 §2.3). To achieve that it inserts
> anonymous **padding bytes**:
>
> ```cpp
> struct Bad {          // offset  size
>     std::uint8_t  a;  //      0     1
>                       //      1     3   <- 3 bytes of padding, inserted silently
>     std::uint32_t b;  //      4     4
>     std::uint8_t  c;  //      8     1
>                       //      9     3   <- 3 more, to make sizeof a multiple of alignof
> };                    // sizeof(Bad) == 12, not 6
> ```
>
> Two rules produce this: each member is aligned to `alignof(member)`, and the struct's total
> size is rounded up to a multiple of the struct's own alignment (so that `Bad arr[2]` keeps
> element 1 aligned too).
>
> Consequences that matter for a file format:
>
> - **Declaration order changes `sizeof`.** Sorting members largest-first often shrinks a
>   struct. `FileHeader` is all `uint32_t`, so it has no padding at all — which is deliberate,
>   not luck.
> - **Padding bytes have indeterminate values.** `memcpy`ing a struct to disk writes whatever
>   was in those gaps: stack garbage, possibly fragments of other data. Two headers with
>   identical fields can differ byte-for-byte, which wrecks file diffing, and it is a genuine
>   information-disclosure vector in software that ships files to other people.
> - **`#pragma pack(1)`** removes padding, and is a trap. It is non-standard, and it produces
>   *misaligned members* — so `&header.numPages` may be an unaligned `uint32_t*`, which is
>   undefined to dereference and faults on some architectures.
>
> The fix is not to control padding, it is to **not depend on it** — which is exactly what §3's
> field-by-field encoding does. The `static_assert(sizeof(FileHeader) == 32)` then guards the
> *in-memory* struct so that adding a field is a build break rather than a silent format change.

### Why a magic number

The first thing `Open` does is check it. Without that check, pointing your engine at
`resume.pdf` reads four arbitrary bytes as `numPages`, believes the file has 3.7 billion
pages, and starts writing structure into someone's document. With the check you get a clean
"not a searchengine database file".

`0x42444553` is `"SEDB"` when written little-endian. Read your file in a hex editor and you
will literally see `SEDB` as the first four characters — which is why real formats do this
(`\x89PNG`, `%PDF`, `SQLite format 3\0`). It makes the format self-identifying to humans and
to `file(1)`, not just to your code.

### Why a version number

The first time you change the page layout — and you will, in doc 05 — every file written by
the old code becomes garbage under the new code. A version field turns that from silent
corruption into a clear error, and later gives you a place to hang migration logic.

The rule: **check `version > SEDB_VERSION` and refuse.** Older versions you may be able to
read; newer ones you definitionally cannot, because they were written by code that knew things
you don't.

### Why store `pageSize` when it's a compile-time constant

Precisely *because* it is a compile-time constant. If someone rebuilds with `PAGE_SIZE = 8192`
and opens an existing 4096-byte-page file, every offset in the engine is wrong and the
corruption is total. The field costs 4 bytes and converts that into an assertion failure on
open. **Any compile-time assumption baked into a persistent format must be recorded in that
format.** This generalises: it is the same reason you would store the key size, the endianness
marker, and the compression codec.

### Why `reserved`, and why the `static_assert` on size

`reserved` pads the struct to a round 32 bytes so that adding a field later does not shift
every subsequent one. The `static_assert` freezes the layout: if a future edit changes the
size, the build breaks *before* you write incompatible files rather than after.

Note we never `memcpy` this struct directly to disk as a struct — §3 serialises it field by
field. The `static_assert` is defence for the *in-memory* representation, so that a stray
field addition is caught.

---

## 3. Serialising the header

```cpp
static void EncodeHeader(const FileHeader& h, Page& page) {
    std::memset(page.data, 0, PAGE_SIZE);       // the other 4064 bytes are defined as zero
    std::size_t off = 0;
    auto put = [&](auto v) { std::memcpy(page.data + off, &v, sizeof(v)); off += sizeof(v); };

    put(h.magic);  put(h.version);  put(h.pageSize);     put(h.numPages);
    put(h.freeListHead);  put(h.rootPageId);  put(h.numFreePages);  put(h.reserved);
}

static FileHeader DecodeHeader(const Page& page) {
    FileHeader h{};
    std::size_t off = 0;
    auto get = [&](auto& v) { std::memcpy(&v, page.data + off, sizeof(v)); off += sizeof(v); };

    get(h.magic);  get(h.version);  get(h.pageSize);     get(h.numPages);
    get(h.freeListHead);  get(h.rootPageId);  get(h.numFreePages);  get(h.reserved);
    return h;
}
```

> **C++ — generic lambdas.** `auto put = [&](auto v) { ... };` — the `auto` *parameter* makes
> this a **generic lambda** (C++14). The compiler generates a templated `operator()`, so one
> lambda handles `uint32_t` and `page_id_t` and anything else you pass, each instantiated
> separately with `sizeof(v)` resolved at compile time.
>
> Without it you would need a template function, which cannot capture `off` by reference as
> conveniently, or eight near-identical lines with the offsets written out by hand — and a
> hand-written offset is exactly the thing that drifts when a field is inserted.
>
> Note `off` is captured by reference and **mutated across calls**, so the eight `put(...)`
> calls walk forward through the page. That works because the lambda's `operator()` is
> `const` by default but the *capture* is a reference to an external `std::size_t`, so
> modifying it is legal. (Had you captured by value and wanted to mutate the copy, you would
> need `[=]() mutable`.)
>
> The reason this compiles to nothing: the lambda is a local object with a known type, every
> call site is visible, and each `memcpy` has a constant size. At `-O2` the whole thing becomes
> eight stores. This is the same "template beats `std::function`" point as doc 01 §6.1 — a
> lambda's type is unique and concrete, which is what lets the optimiser see through it.

Field-by-field `memcpy`, not one `memcpy` of the whole struct. Why, when the `static_assert`
already guarantees the size?

Because size is not layout. Two compilers can agree that `sizeof(FileHeader) == 32` and
disagree about where the padding sits. Explicit encoding makes the on-disk order **yours**,
independent of any ABI, and it gives you the one place to insert byte-swapping if you ever
need cross-architecture files. It costs nothing at `-O2` — the compiler collapses eight
fixed-size `memcpy`s into a handful of stores.

The `memset` first matters too: it defines the remaining 4064 bytes as zero rather than
whatever was in the buffer. Uninitialised bytes written to disk are how secrets leak out of
processes, and how "the file differs but the data is the same" wrecks your ability to diff
two database files during debugging.

---

## 4. The free list, stored in the free pages

This is the idea worth the doc.

A free page has no useful content by definition. So use its first four bytes to hold the id of
the *next* free page. The header holds the id of the first. That is a singly-linked list whose
nodes cost nothing, because they live in space that is already wasted.

```
  header.freeListHead = 7

     page 7            page 12            page 3
   +--------+        +--------+        +--------+
   |  12    | -----> |   3    | -----> | 0xFFFF |   <- INVALID_PAGE_ID terminates
   | (junk) |        | (junk) |        | (junk) |
   +--------+        +--------+        +--------+
```

```cpp
page_id_t DiskManager::AllocatePage() {
    if (m_Header.freeListHead != INVALID_PAGE_ID) {
        // Pop the head of the free list.
        const page_id_t id = m_Header.freeListHead;

        // The next pointer lives in the first 4 bytes of the page being reused. We must read
        // it BEFORE handing the page out -- the caller is about to overwrite those bytes.
        Page p;
        ReadPage(id, p);
        page_id_t next;
        std::memcpy(&next, p.data, sizeof(next));

        m_Header.freeListHead = next;
        m_Header.numFreePages--;
        m_HeaderDirty = true;

        // Hand back a clean page. Without this the caller inherits the previous occupant's
        // bytes, and a bug that forgets to initialise one field reads plausible-looking
        // garbage from a node that used to live here. Zeroing turns that into an obvious
        // zero rather than a convincing lie.
        std::memset(p.data, 0, PAGE_SIZE);
        WritePage(id, p);
        return id;
    }

    // Free list empty: extend the file.
    const page_id_t id = m_Header.numPages;
    m_Header.numPages++;
    m_HeaderDirty = true;

    Page blank;
    std::memset(blank.data, 0, PAGE_SIZE);
    WritePage(id, blank);          // materialise it so NumPages() and the file agree
    return id;
}

void DiskManager::DeallocatePage(page_id_t id) {
    if (id == HEADER_PAGE_ID || id == INVALID_PAGE_ID || id >= m_Header.numPages) {
        throw std::runtime_error("DeallocatePage: refusing to free page "
                                 + std::to_string(id));
    }

    Page p;
    std::memset(p.data, 0, PAGE_SIZE);
    std::memcpy(p.data, &m_Header.freeListHead, sizeof(page_id_t));   // old head becomes next
    WritePage(id, p);

    m_Header.freeListHead = id;                                       // this page is new head
    m_Header.numFreePages++;
    m_HeaderDirty = true;
}
```

### Why LIFO and not FIFO

Pushing and popping at the head makes both operations O(1) with a single pointer. A FIFO would
need a tail pointer and would touch two pages per operation.

The cost is a real one, and doc 01 §5 named it: **LIFO reuse scrambles locality.** Pages come
back in reverse order of freeing, so over a long insert/delete workload, logically adjacent
leaves end up physically scattered. Your leaf-chain range scan degrades from sequential I/O
toward random I/O — a 15× difference on NVMe.

Real engines fight this with periodic compaction, or by allocating in multi-page **extents**
so that at least runs of pages stay contiguous. We accept the fragmentation and *measure* it
in doc 11 §6, which is the honest order: build the simple thing, measure the damage, then
decide whether the fix is worth it.

### Why zero the page on both paths

On free, we zero (except the next pointer) so that stale key data is not sitting readable in
the file. On allocate, we zero again so the caller starts clean. Two writes where one might
do — and worth it, because the alternative is that doc 05's `NodePage` reads a stale
`keyCount` from a page it thinks is blank and walks off into another node's data. Silent wrong
answers again.

If you later find these writes on your profile, the fix is not to remove them; it is to make
allocation return a *buffer-pool frame* that is zeroed in memory and never round-trips to disk
at all. Doc 06 does exactly that.

### Why refuse to free page 0

Freeing the header would put the header on the free list, hand it out as an ordinary page, and
destroy the file. The guard is three comparisons and it prevents an unrecoverable bug. Guard
your invariants at the boundary where they can still be cheaply checked.

---

## 5. The bootstrap problem

There is a chicken-and-egg on a brand-new file: `AllocatePage` reads `m_Header`, but the header
must itself live on a page that was never allocated.

The resolution is that **page 0 is not allocated, it is asserted**. On open:

```cpp
void DiskManager::LoadOrInitHeader() {
    if (NumPages() == 0) {
        // Brand new file. Build a header describing a file with exactly one page: itself.
        m_Header.magic        = SEDB_MAGIC;
        m_Header.version      = SEDB_VERSION;
        m_Header.pageSize     = static_cast<std::uint32_t>(PAGE_SIZE);
        m_Header.numPages     = 1;                  // page 0 exists and is this header
        m_Header.freeListHead = INVALID_PAGE_ID;
        m_Header.rootPageId   = INVALID_PAGE_ID;    // no tree yet
        m_Header.numFreePages = 0;
        m_Header.reserved     = 0;

        m_HeaderDirty = true;
        FlushHeader();
        Sync();          // a half-created file with no valid header is unopenable; pay 5 ms once
        return;
    }

    Page p;
    ReadPage(HEADER_PAGE_ID, p);
    m_Header = DecodeHeader(p);

    if (m_Header.magic != SEDB_MAGIC) {
        throw std::runtime_error("not a searchengine database file: " + m_Path);
    }
    if (m_Header.version > SEDB_VERSION) {
        throw std::runtime_error("file was written by a newer version ("
                                 + std::to_string(m_Header.version) + ")");
    }
    if (m_Header.pageSize != PAGE_SIZE) {
        throw std::runtime_error("file uses page size " + std::to_string(m_Header.pageSize)
                                 + ", this build uses " + std::to_string(PAGE_SIZE));
    }

    // numPages is a CACHE of a fact the filesystem already knows. If they disagree, the
    // filesystem is right -- it survived whatever crash we didn't. See section 6.
    const std::size_t actual = NumPages();
    if (m_Header.numPages > actual) {
        m_Header.numPages = static_cast<std::uint32_t>(actual);
        m_HeaderDirty = true;
    }
}
```

The last check is the interesting one, and it introduces the idea the next section is about.

---

## 6. Which metadata can you afford to lose?

You now have three pieces of state in the header, and they have **completely different
failure characteristics**. Recognising this distinction is the difference between a storage
engine that degrades and one that corrupts.

| Field | If it is stale after a crash | Severity |
|---|---|---|
| `numPages` | Recompute from file size. Always recoverable. | **None** — it is a cache |
| `freeListHead` | Some freed pages are never reused. File is larger than necessary. | **Benign** — a leak |
| `rootPageId` | The tree is unreachable. Every key still on disk, no way to find any of them. | **Fatal** |

This asymmetry buys you a big optimisation and tells you exactly where the limit is:

- **You may keep `numPages` and `freeListHead` dirty in memory** and flush them lazily. The
  worst case is a slightly larger file. That saves a header write on every single allocation —
  and allocations happen on every split.
- **You may not do that with `rootPageId`.** When the root changes (doc 10: the tree grows a
  level), the new root id must reach the disk *before* you consider the operation complete.

That single sentence is the seed of write-ahead logging. A WAL exists because *some* state
transitions must be atomic and durable, and the cheapest way to make an arbitrary update
atomic is to write down what you are about to do, `fsync` that, and only then do it. Doc 11
§4 builds the minimal version.

```cpp
void DiskManager::FlushHeader() {
    if (!m_HeaderDirty) return;
    Page p;
    EncodeHeader(m_Header, p);
    WritePage(HEADER_PAGE_ID, p);
    m_HeaderDirty = false;
}

void DiskManager::SetRootPageId(page_id_t id) {
    m_Header.rootPageId = id;
    m_HeaderDirty = true;
    FlushHeader();        // fatal-if-lost: write it through immediately
    // NOTE: still not durable until Sync(). Doc 11 section 4 closes that gap.
}
```

---

## 7. Where the header write is the commit point

Look at the structure that has emerged, because it is the shape of every transactional storage
system:

1. Write new/modified data pages anywhere in the file. **None of it is reachable yet** — the
   header still points at the old root.
2. `Sync()` — those data pages are now durably on the device, but still unreferenced.
3. Write the header with the new `rootPageId`. **One page, one atomic-ish write.** This is the
   instant the change becomes visible.
4. `Sync()` again.

Crash before step 3 and you have some orphaned pages (a leak) and a perfectly intact old tree.
Crash after step 3 and you have the new tree. There is no in-between state where the tree is
half-updated — **because reachability funnels through a single page.**

That is **copy-on-write / shadow paging**, and it is how LMDB and modern SQLite's WAL mode get
crash safety. You have built the skeleton of it without meaning to, simply by having one page
that everything hangs off.

> The caveat that keeps this honest: a single 4096-byte write is *usually* atomic on modern
> drives but is not guaranteed to be. A torn header is unrecoverable. Real systems either
> write two alternating header pages with checksums and pick the newest valid one, or put the
> commit record in a log. Doc 11 §5 implements the two-header trick — it is about fifteen
> lines and it is the difference between "probably fine" and "provably fine".

---

## 8. Additions to the class

```cpp
// DiskManager.hpp -- additions
public:
    page_id_t AllocatePage();
    void      DeallocatePage(page_id_t id);

    page_id_t RootPageId() const { return m_Header.rootPageId; }
    void      SetRootPageId(page_id_t id);

    std::uint32_t FreePageCount() const { return m_Header.numFreePages; }

    void Sync();                       // now also flushes the header first
    bool ValidateFreeList() const;     // walks the chain; see the checkpoint

private:
    void LoadOrInitHeader();
    void FlushHeader();

    FileHeader m_Header{};
    bool       m_HeaderDirty = false;
```

`Sync()` gains one line, and the ordering of that line is the whole point:

```cpp
void DiskManager::Sync() {
    FlushHeader();          // header must be IN the file before we force the file to disk
    std::fflush(m_File);
#if defined(_WIN32)
    if (_commit(_fileno(m_File)) != 0) throw std::runtime_error("Sync: _commit failed");
#else
    if (::fsync(::fileno(m_File)) != 0) throw std::runtime_error("Sync: fsync failed");
#endif
}
```

Flushing the header *after* the `fsync` would be a bug that only shows up on power loss: the
data would be durable and the header describing it would not.

Call `Sync()` in the destructor too — but wrapped in `try/catch`, because destructors must not
throw.

---

## Checkpoint

`storage/tests/diskmanager_alloc_test.cpp`:

```cpp
#include "../DiskManager.hpp"
#include <cassert>
#include <iostream>
#include <set>
#include <vector>

int main() {
    const std::string path = "dm_alloc.db";
    std::remove(path.c_str());

    {
        DiskManager dm(path);
        assert(dm.NumPages() == 1);                    // just the header
        assert(dm.RootPageId() == INVALID_PAGE_ID);

        // Fresh allocations are consecutive and never page 0.
        std::vector<page_id_t> ids;
        for (int i = 0; i < 10; ++i) {
            page_id_t id = dm.AllocatePage();
            assert(id != HEADER_PAGE_ID);
            ids.push_back(id);
        }
        assert(std::set<page_id_t>(ids.begin(), ids.end()).size() == 10);   // all distinct

        // Free three, then reallocate: must reuse, not grow.
        const std::size_t before = dm.NumPages();
        dm.DeallocatePage(ids[2]);
        dm.DeallocatePage(ids[5]);
        dm.DeallocatePage(ids[7]);
        assert(dm.FreePageCount() == 3);
        assert(dm.ValidateFreeList());

        std::set<page_id_t> reused;
        for (int i = 0; i < 3; ++i) reused.insert(dm.AllocatePage());
        assert(dm.NumPages() == before);               // file did NOT grow
        assert(reused == std::set<page_id_t>({ids[2], ids[5], ids[7]}));
        assert(dm.FreePageCount() == 0);

        // LIFO order: last freed is first returned.
        dm.DeallocatePage(ids[1]);
        dm.DeallocatePage(ids[3]);
        assert(dm.AllocatePage() == ids[3]);

        dm.SetRootPageId(42);
        dm.Sync();
    }

    {   // reopen: header state must have survived
        DiskManager dm(path);
        assert(dm.RootPageId() == 42);
        assert(dm.ValidateFreeList());
        std::cout << "alloc test OK (" << dm.NumPages() << " pages, "
                  << dm.FreePageCount() << " free)\n";
    }

    // A file that is not ours must be rejected, not misread.
    {
        std::FILE* f = std::fopen("junk.db", "wb");
        std::vector<char> junk(PAGE_SIZE * 3, 'Z');
        std::fwrite(junk.data(), 1, junk.size(), f);
        std::fclose(f);

        bool threw = false;
        try { DiskManager dm("junk.db"); } catch (const std::exception&) { threw = true; }
        assert(threw);
        std::remove("junk.db");
    }

    std::cout << "diskmanager_alloc_test OK\n";
}
```

Write `ValidateFreeList()` yourself — it walks from `freeListHead` following the embedded next
pointers, counting hops, and returns false if the count disagrees with `numFreePages`, if any
id is out of range, or if it exceeds `numPages` hops (which means the list has a cycle).
That last check is not paranoia: a double-free creates a cycle, and without the hop cap your
validator hangs instead of reporting.

Before doc 05, you should have:

- [ ] Allocation reusing freed pages, with the file not growing
- [ ] Header surviving reopen, magic and version rejecting foreign files
- [ ] `ValidateFreeList()` detecting a deliberately corrupted `freeListHead`
- [ ] An answer to: *which header field is fatal to lose, which is merely a leak, and which is
      not really state at all?*
- [ ] An answer to: *why does writing the header last make the update atomic?*

Next: [05 — Node Page Layout](05-node-page-layout.md), where 4096 raw bytes finally become a
B+Tree node and you count every byte you spend.
