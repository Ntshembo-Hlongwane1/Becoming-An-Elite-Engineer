# 05 — Node Page Layout

> **Build target:** `internal/kernal/core/storage/NodePage.hpp` — a *view* over 4096 raw bytes
> that lets you read and write a B+Tree node without ever owning memory. About 200 lines, most
> of them accessors. At the end you will serialise a node, read it back through a fresh
> `DiskManager`, and get identical keys out.
>
> **This is the doc where latency is decided.** Every byte you spend on a header is a key you
> cannot store; every key you cannot store raises the tree's height; every level of height is
> another 20 µs cold read on the critical path. We are going to count bytes, and we are going
> to arrange them so that a binary search touches as few cache lines as physically possible.

---

## 1. Why the disk tree is not a template

Your in-memory tree is `template<typename KeyType> class BPlusTree`. The disk tree will not
be. This is worth a section because it looks like a regression and is not.

A template parameter is a **compile-time promise about a type's behaviour**: that it has
`operator<`, that it can be copied. A page format is a **runtime promise about a type's
bytes**: that a key occupies exactly 8 bytes, at a known offset, in a known order.

Those are different kinds of promise, and `std::string` is where the gap becomes obvious:

```cpp
// sizeof(std::string) is 32 on libstdc++. What are those 32 bytes?
//   - a POINTER to heap-allocated characters
//   - a length
//   - a small-string-optimisation buffer
```

`memcpy` that to disk and you have written **a pointer** — an address in a process that no
longer exists — plus a length that describes memory nobody owns. Read it back and dereference
it and you get whatever now lives at that address. It will not even reliably crash.

So the disk format commits to fixed-size keys:

```cpp
using disk_key_t = std::uint64_t;
```

For a search engine that is not a limitation, it is the design: the term dictionary maps a
**term id** (a `uint64` hash or dense id assigned at index time) to its posting list. Strings
live in a separate string table. That indirection is what Lucene, Postgres, and every serious
engine do, and §8 sketches the variable-length version for when you need it.

> **The general principle, worth carrying beyond this project:** a serialisation boundary is
> where generic code stops. Templates give you flexibility across types; a file format needs
> agreement about bytes. Somewhere the two must meet, and that place should be explicit, small,
> and auditable — not spread through a template that silently does the wrong thing for any
> type containing a pointer.

---

## 2. The node header — 16 bytes, and every one is argued for

```cpp
// Byte layout of every node page:
//
//   offset  size  field
//   ------  ----  ------------------------------------------------------------
//        0     2  nodeType     0 = internal, 1 = leaf
//        2     2  keyCount     number of keys currently stored
//        4     4  nextLeaf     leaf only: next leaf in the chain, else INVALID
//        8     4  prevLeaf     leaf only: previous leaf, else INVALID
//       12     4  reserved     padding to 16; future checksum lives here
//   ------  ----
//             16  total
```

### Why `keyCount` is `uint16_t` and not `uint32_t`

The maximum keys per page is 339 (computed in §4). A `uint16_t` holds 65535. Using `uint32_t`
would waste 2 bytes — which is a quarter of one key, forever, on every page in the file. At
half a million pages that is a megabyte spent on nothing.

**Size every field to its actual range, not to the machine word.** This is the habit that
separates a format that gets fanout 339 from one that gets 320.

### Why there is no `parentPageId` — a real change from your in-memory tree

Your `BPlusTreeNode` has `BPlusTreeNode* parent`. The disk node deliberately does not, and the
reasoning is pure latency:

Consider `SplitInternal` moving 170 children to a new node. With parent pointers on disk, each
of those 170 children must have its parent field updated — **170 page writes**, each of which
dirties a page that must eventually be flushed. Without them: zero writes.

What replaces it? A **path stack** recorded during descent. `FindLeaf` already visits root →
… → leaf; it just remembers the page ids it passed through. When a split needs to insert into
the parent, it pops the stack. The information was free — you already had it — and it is
always correct, whereas a stored parent pointer is a second copy of the truth that can drift.

```cpp
// doc 09 builds this; noted here so you understand why the field is absent
struct SearchPath {
    std::vector<page_id_t> nodes;    // root .. leaf
    std::vector<int>       slots;    // which child index we took at each level
};
```

This is what real B+Trees do, and it is the single biggest structural difference between the
textbook in-memory tree you wrote and a production on-disk one.

### Why `nextLeaf` and `prevLeaf` sit in the common header

They are meaningless for internal nodes, so this "wastes" 8 bytes on every internal page.
Accepted deliberately: internal pages are roughly 0.3% of all pages (one internal per ~200
leaves), so the waste is 8 bytes × 0.3% — nothing. In exchange, every node has an identical
16-byte header, so the code that reads a header does not first need to know what kind of node
it is reading. **Uniformity at the parse boundary is worth more than 8 bytes on a rare page.**

### Why `reserved`

A checksum goes here when you want to detect torn writes (doc 11 §5). Reserving the space now
means adding it later does not shift every field and invalidate every existing file.

---

## 3. The two payload layouts

```
INTERNAL PAGE                                       n = keyCount
+----------------+---------------------------+-------------------------------+
| header (16 B)  | keys[n]  (8 B each)        | children[n+1]  (4 B each)     |
+----------------+---------------------------+-------------------------------+
 0               16                          16 + 8n                     4096

  Same semantics as your in-memory tree: n keys, n+1 children, and
  children[i] holds keys in the window  [ keys[i-1], keys[i] ).


LEAF PAGE                                           n = keyCount
+----------------+---------------------------+-------------------------------+
| header (16 B)  | keys[n]  (8 B each)        | values[n]  (12 B each)        |
+----------------+---------------------------+-------------------------------+
 0               16                          16 + 8n                     4096

  values[i] is a PostingRef, not a posting list -- see below.
```

### The value type

```cpp
// Where a term's posting list lives in the postings file. 12 bytes on disk.
struct PostingRef {
    std::uint64_t offset;      // byte offset into postings.dat
    std::uint32_t length;      // byte length of the encoded posting list
};
```

This is the one place the disk tree's semantics differ from your in-memory tree, and the
change is forced by physics rather than taste.

In memory you stored `std::vector<RecordID>` per key — the posting list lives *in* the leaf.
On disk that cannot work: a page is 4096 bytes and a common term's posting list is megabytes.
A single entry would overflow the node and the whole fixed-size-page design collapses.

So the leaf stores a **reference**, and the list lives in a separate append-only file. Appending
a document to a term's postings becomes: append to `postings.dat`, then update 12 bytes in the
leaf. The tree's shape never changes on an append — only on a *new term*.

This is exactly how a real inverted index works, and it is a genuinely better design than what
you had. Your `Insert(key, record)` splits into two operations with different costs, and that
split is honest: adding a document to an existing term is cheap, and introducing a new term is
the expensive one.

---

## 4. Counting the bytes — where fanout actually comes from

**Internal page.** We need the largest `n` satisfying:

```
    16 + 8n + 4(n+1)  <=  4096
    16 + 8n + 4n + 4  <=  4096
              12n     <=  4076
                n     <=  339.67
                n      =  339            ->  340 children
```

**Leaf page.** Each entry is a key plus a `PostingRef`:

```
    16 + n(8 + 12)  <=  4096
            20n     <=  4080
              n      =  204              ->  exactly 4096 bytes used, zero waste
```

```cpp
inline constexpr std::size_t NODE_HEADER_SIZE = 16;
inline constexpr std::size_t KEY_SIZE         = sizeof(disk_key_t);      // 8
inline constexpr std::size_t CHILD_SIZE       = sizeof(page_id_t);       // 4
inline constexpr std::size_t VALUE_SIZE       = 12;                      // PostingRef, packed

inline constexpr std::size_t MAX_INTERNAL_KEYS =
    (PAGE_SIZE - NODE_HEADER_SIZE - CHILD_SIZE) / (KEY_SIZE + CHILD_SIZE);   // 339

inline constexpr std::size_t MAX_LEAF_KEYS =
    (PAGE_SIZE - NODE_HEADER_SIZE) / (KEY_SIZE + VALUE_SIZE);                // 204

inline constexpr std::size_t MIN_INTERNAL_KEYS = MAX_INTERNAL_KEYS / 2;      // 169
inline constexpr std::size_t MIN_LEAF_KEYS     = MAX_LEAF_KEYS / 2;          // 102

// Prove the arithmetic at compile time, so a later change to any constant cannot silently
// produce a node that overruns its page.
static_assert(NODE_HEADER_SIZE + MAX_INTERNAL_KEYS * KEY_SIZE
              + (MAX_INTERNAL_KEYS + 1) * CHILD_SIZE <= PAGE_SIZE);
static_assert(NODE_HEADER_SIZE + MAX_LEAF_KEYS * (KEY_SIZE + VALUE_SIZE) <= PAGE_SIZE);
```

**These constants replace `m_Order`.** Notice what happened: in your in-memory tree the order
was a constructor argument you chose. Here it is *derived* — from the page size, the key size,
and the pointer size, none of which you are free to pick. The physical layer decides the
shape of the algorithm. That inversion is the whole lesson of this series in one paragraph.

### What it buys

```
  100,000,000 keys, leaf fanout 204, internal fanout 340:

     leaves          100,000,000 / 204  =  490,197
     level above         490,197 / 340  =    1,442
     level above           1,442 / 340  =        5
     root                                        1

  -> 4 levels -> 4 cold reads -> ~80 us per lookup at your doc 01 number.
```

Your in-memory tree at order 4 would need 14 levels for the same data. The entire difference
is the arithmetic above.

---

## 5. Why keys and values are separate arrays

This is the layout decision with the largest measurable effect, and it is invisible unless you
think in cache lines.

**Array of structs (AoS)** — the layout you would write without thinking:

```
   [k0|v0][k1|v1][k2|v2] ...       each pair 20 bytes, keys 20 bytes apart
```

**Struct of arrays (SoA)** — what we are doing:

```
   [k0][k1][k2]...[k203] [v0][v1][v2]...[v203]      keys 8 bytes apart, contiguous
```

Now count cache lines for a binary search, the operation you do on **every level of every
lookup**. A cache line is 64 bytes.

| | AoS | SoA |
|---|---|---|
| All keys span | 4080 bytes = 64 lines | 1632 bytes = **26 lines** |
| Keys per line | 3 (rest is value bytes) | **8** |
| Useful bytes per line fetched | 24 of 64 = 37% | **64 of 64 = 100%** |

Binary search over 204 keys is ~8 probes. Under AoS each probe pulls a 64-byte line of which
40 bytes are `PostingRef` data you are not going to look at. Under SoA every byte fetched is a
key, and the final few probes — which land close together — often hit lines already loaded.

**You only need the value once, after the search has finished.** Fetching values alongside
keys is fetching data you have an 8-in-204 chance of wanting.

> This generalises far past B+Trees. Any time you search or filter on one field of a record,
> ask whether that field wants to live in its own array. It is the core idea behind columnar
> databases (Parquet, ClickHouse), and behind the entity-component-system layouts games use.
> Same insight, three industries.

The cost of SoA: inserting at position `i` requires shifting two arrays instead of one. That
is two `memmove` calls instead of one — and `memmove` is bandwidth-bound and vectorised, so
two calls over 1632 and 2448 bytes cost about the same as one over 4080. **You pay nothing and
you gain 2.5× on the search path.**

---

## 6. The `NodePage` view

The class owns nothing. It wraps a `Page&` and interprets it. That is the whole design.

```cpp
#pragma once
// internal/kernal/core/storage/NodePage.hpp
#include <cstring>
#include <cstdint>
#include <cassert>
#include "Page.hpp"

using disk_key_t = std::uint64_t;

struct PostingRef {
    std::uint64_t offset = 0;
    std::uint32_t length = 0;
};

enum class NodeType : std::uint16_t { Internal = 0, Leaf = 1 };

// ... the constants from section 4 ...

class NodePage {
public:
    explicit NodePage(Page& page) : m_Page(page) {}

    // ---------------- header ----------------
    NodeType Type() const           { return static_cast<NodeType>(ReadU16(0)); }
    void SetType(NodeType t)        { WriteU16(0, static_cast<std::uint16_t>(t)); }

    bool IsLeaf() const             { return Type() == NodeType::Leaf; }

    std::size_t KeyCount() const    { return ReadU16(2); }
    void SetKeyCount(std::size_t n) {
        assert(n <= (IsLeaf() ? MAX_LEAF_KEYS : MAX_INTERNAL_KEYS));
        WriteU16(2, static_cast<std::uint16_t>(n));
    }

    page_id_t NextLeaf() const      { return ReadU32(4); }
    void SetNextLeaf(page_id_t id)  { WriteU32(4, id); }
    page_id_t PrevLeaf() const      { return ReadU32(8); }
    void SetPrevLeaf(page_id_t id)  { WriteU32(8, id); }

    // Call once on a freshly allocated page, before anything else touches it.
    void Init(NodeType type) {
        std::memset(m_Page.data, 0, PAGE_SIZE);
        SetType(type);
        SetKeyCount(0);
        SetNextLeaf(INVALID_PAGE_ID);
        SetPrevLeaf(INVALID_PAGE_ID);
    }

    std::size_t MaxKeys() const { return IsLeaf() ? MAX_LEAF_KEYS : MAX_INTERNAL_KEYS; }
    std::size_t MinKeys() const { return IsLeaf() ? MIN_LEAF_KEYS : MIN_INTERNAL_KEYS; }
    bool IsFull() const         { return KeyCount() >= MaxKeys(); }

    // ---------------- keys (both node types) ----------------
    disk_key_t KeyAt(std::size_t i) const {
        assert(i < KeyCount());
        disk_key_t k;
        std::memcpy(&k, m_Page.data + KeyOffset(i), KEY_SIZE);
        return k;
    }
    void SetKeyAt(std::size_t i, disk_key_t k) {
        assert(i < MaxKeys());
        std::memcpy(m_Page.data + KeyOffset(i), &k, KEY_SIZE);
    }

    // ---------------- children (internal only) ----------------
    page_id_t ChildAt(std::size_t i) const {
        assert(!IsLeaf() && i <= KeyCount());
        return ReadU32(ChildOffset(i));
    }
    void SetChildAt(std::size_t i, page_id_t id) {
        assert(!IsLeaf() && i <= MAX_INTERNAL_KEYS);
        WriteU32(ChildOffset(i), id);
    }

    // ---------------- values (leaf only) ----------------
    PostingRef ValueAt(std::size_t i) const {
        assert(IsLeaf() && i < KeyCount());
        PostingRef v;
        const std::size_t off = ValueOffset(i);
        std::memcpy(&v.offset, m_Page.data + off,     sizeof(v.offset));
        std::memcpy(&v.length, m_Page.data + off + 8, sizeof(v.length));
        return v;
    }
    void SetValueAt(std::size_t i, const PostingRef& v) {
        assert(IsLeaf() && i < MAX_LEAF_KEYS);
        const std::size_t off = ValueOffset(i);
        std::memcpy(m_Page.data + off,     &v.offset, sizeof(v.offset));
        std::memcpy(m_Page.data + off + 8, &v.length, sizeof(v.length));
    }

    // ---------------- the two searches, unchanged in spirit from your in-memory tree ------
    // First index i with key < keys[i], or n. Descent uses this: equal goes RIGHT.
    std::size_t UpperBoundIndex(disk_key_t key) const {
        std::size_t lo = 0, hi = KeyCount();
        while (lo < hi) {
            std::size_t mid = lo + (hi - lo) / 2;
            if (key < KeyAt(mid)) hi = mid; else lo = mid + 1;
        }
        return lo;
    }

    // First index i with keys[i] >= key, or n. Leaf slot lookup uses this.
    std::size_t LowerBoundIndex(disk_key_t key) const {
        std::size_t lo = 0, hi = KeyCount();
        while (lo < hi) {
            std::size_t mid = lo + (hi - lo) / 2;
            if (KeyAt(mid) < key) lo = mid + 1; else hi = mid;
        }
        return lo;
    }

private:
    static std::size_t KeyOffset(std::size_t i)   { return NODE_HEADER_SIZE + i * KEY_SIZE; }
    std::size_t ChildOffset(std::size_t i) const {
        return NODE_HEADER_SIZE + MAX_INTERNAL_KEYS * KEY_SIZE + i * CHILD_SIZE;
    }
    std::size_t ValueOffset(std::size_t i) const {
        return NODE_HEADER_SIZE + MAX_LEAF_KEYS * KEY_SIZE + i * VALUE_SIZE;
    }

    std::uint16_t ReadU16(std::size_t off) const {
        std::uint16_t v; std::memcpy(&v, m_Page.data + off, 2); return v;
    }
    void WriteU16(std::size_t off, std::uint16_t v) {
        std::memcpy(m_Page.data + off, &v, 2);
    }
    std::uint32_t ReadU32(std::size_t off) const {
        std::uint32_t v; std::memcpy(&v, m_Page.data + off, 4); return v;
    }
    void WriteU32(std::size_t off, std::uint32_t v) {
        std::memcpy(m_Page.data + off, &v, 4);
    }

    Page& m_Page;
};
```

### Three things to notice

**The offsets are computed from `MAX_*_KEYS`, not `KeyCount()`.** The children array starts
after space for the *maximum* number of keys, not the current number. If it started after the
current keys, then adding a key would move the entire children array — every insert would
rewrite the whole page. Fixed offsets mean an insert shifts only the tail of two arrays.

The cost is that a half-full page has a gap in the middle. That is fine: pages are fixed-size
anyway, so the space is not recoverable for anything else.

**Every read and write is a `memcpy`.** Doc 02 §4 explained why: keys sit at offset
`16 + 8i`, which is 8-byte-aligned only by luck of the header size, and a pointer cast would
be undefined behaviour regardless. At `-O2` these compile to single `mov` instructions —
verify it on godbolt once and you will stop worrying about it.

**The binary searches are byte-for-byte the ones you already wrote**, in the boundary-template
form. `UpperBoundIndex` for descent, `LowerBoundIndex` for leaf slots, equal-goes-right vs
equal-lands-on-slot. Every bug you already fixed in the in-memory version, you have now
already fixed here.

---

## 7. Sizing revisited — is 8-byte keys the right call?

Redo §4's arithmetic for a few key sizes and watch the height:

| Key size | Internal fanout | Leaf fanout | Levels for 10⁸ keys | Lookup @ 20 µs |
|---|---|---|---|---|
| 4 bytes | 509 | 255 | 4 | 80 µs |
| **8 bytes** | **339** | **204** | **4** | **80 µs** |
| 16 bytes | 203 | 145 | 4 | 80 µs |
| 32 bytes | 113 | 92 | 4 | 80 µs |
| 64 bytes | 59 | 53 | **5** | 100 µs |

The lesson is not "smaller is always better" — it is that **fanout enters as a logarithm, so
it takes a large change to move the height at all.** Everything from 4 bytes to 32 bytes gives
the identical height. Shrinking the key buys you nothing; the first real cost appears at 64
bytes, where you cross into a fifth level and pay 25% more latency.

So: 8 bytes sits comfortably in the middle of the flat region and gives you a full 64-bit id
space. That is the argument. Notice it is an argument from a table, not from taste — and
notice that the table is where the *intuition* ("bigger keys must be slower") turns out to be
wrong across most of the range. Recompute it yourself rather than trusting either the table or
the intuition.

---

## 8. Variable-length keys, when you need them

Fixed-size keys are the right start. When you eventually want raw terms in the tree, the
mechanism is a **slot directory**, and it is worth knowing the shape now:

```
+--------+------------------+.............+------------------------+
| header | slots[] ->       |  free space | <- key bytes grow down |
+--------+------------------+.............+------------------------+
          each slot: (offset:u16, length:u16)      variable-length keys packed at the end
```

Slots are fixed-size and sorted by key, so binary search still works — you binary search the
*slot array*, following each slot's offset to compare the actual bytes. Keys are appended at
the far end of the page and never move; only the small slot array shifts on insert.

This is precisely how PostgreSQL, SQLite, and InnoDB lay out pages, and it is why they all
call it a "slotted page". The cost is one extra indirection per comparison — the key bytes are
somewhere else in the page, so you take a second cache line per probe. That is a real,
measurable regression on the search path, which is why you use fixed-size keys whenever you
can get away with it.

---

## Checkpoint

`storage/tests/nodepage_test.cpp`:

```cpp
#include "../NodePage.hpp"
#include "../DiskManager.hpp"
#include <cassert>
#include <iostream>
#include <memory>

int main() {
    std::cout << "MAX_INTERNAL_KEYS = " << MAX_INTERNAL_KEYS << "\n"
              << "MAX_LEAF_KEYS     = " << MAX_LEAF_KEYS     << "\n";
    assert(MAX_INTERNAL_KEYS == 339);
    assert(MAX_LEAF_KEYS     == 204);

    std::remove("node_test.db");
    auto page = std::make_unique<Page>();

    // ---- leaf round-trip through a real file, in a separate scope so it closes ----
    {
        NodePage leaf(*page);
        leaf.Init(NodeType::Leaf);
        assert(leaf.IsLeaf() && leaf.KeyCount() == 0);

        leaf.SetKeyCount(MAX_LEAF_KEYS);
        for (std::size_t i = 0; i < MAX_LEAF_KEYS; ++i) {
            leaf.SetKeyAt(i, static_cast<disk_key_t>(i) * 10);
            leaf.SetValueAt(i, PostingRef{ i * 1000ull, static_cast<std::uint32_t>(i) });
        }
        leaf.SetNextLeaf(77);

        DiskManager dm("node_test.db");
        page_id_t id = dm.AllocatePage();
        dm.WritePage(id, *page);
        dm.SetRootPageId(id);
        dm.Sync();
    }

    // ---- read it back through a FRESH DiskManager: this is the real test ----
    {
        DiskManager dm("node_test.db");
        auto back = std::make_unique<Page>();
        dm.ReadPage(dm.RootPageId(), *back);

        NodePage leaf(*back);
        assert(leaf.IsLeaf());
        assert(leaf.KeyCount() == MAX_LEAF_KEYS);
        assert(leaf.NextLeaf() == 77);
        for (std::size_t i = 0; i < MAX_LEAF_KEYS; ++i) {
            assert(leaf.KeyAt(i) == static_cast<disk_key_t>(i) * 10);
            assert(leaf.ValueAt(i).offset == i * 1000ull);
            assert(leaf.ValueAt(i).length == i);
        }

        // Searches must agree with your in-memory tree's semantics.
        assert(leaf.UpperBoundIndex(0)  == 1);    // equal goes RIGHT
        assert(leaf.LowerBoundIndex(0)  == 0);    // equal lands ON the slot
        assert(leaf.UpperBoundIndex(15) == 2);    // between 10 and 20
        assert(leaf.LowerBoundIndex(15) == 2);
        assert(leaf.UpperBoundIndex(99999) == MAX_LEAF_KEYS);
    }

    // ---- internal node: n keys, n+1 children ----
    {
        auto ip = std::make_unique<Page>();
        NodePage node(*ip);
        node.Init(NodeType::Internal);
        node.SetKeyCount(MAX_INTERNAL_KEYS);
        for (std::size_t i = 0; i < MAX_INTERNAL_KEYS; ++i)
            node.SetKeyAt(i, static_cast<disk_key_t>(i));
        for (std::size_t i = 0; i <= MAX_INTERNAL_KEYS; ++i)
            node.SetChildAt(i, static_cast<page_id_t>(1000 + i));

        // The last child must not have overwritten the first key -- proves the offset
        // arithmetic in section 6 leaves no overlap.
        assert(node.KeyAt(0) == 0);
        assert(node.ChildAt(MAX_INTERNAL_KEYS) == 1000 + MAX_INTERNAL_KEYS);
    }

    std::cout << "nodepage_test OK\n";
}
```

Before doc 06, you should have:

- [ ] `MAX_INTERNAL_KEYS == 339` and `MAX_LEAF_KEYS == 204` printing from your own build
- [ ] A leaf surviving a full write → close → reopen → read cycle with every key intact
- [ ] Your fanout table from §7 recomputed by hand for at least one other key size
- [ ] An answer to: *why does the children array start at a fixed offset rather than after the
      current keys?*
- [ ] An answer to: *why is there no `parentPageId`, and what replaces it?*
- [ ] An answer to: *why are keys and values separate arrays?* — if you can state the cache
      line argument in one sentence, you have the most transferable idea in this series.

Next: [06 — BufferPool I: Frames](06-bufferpool-core.md), where pages stop being read twice.
