# 08 — Disk & Persistence

> **This is where the B+Tree stops being a data structure and becomes a database.** Frontier
> doc 02 gave you pages, offsets, and the mmap-vs-buffer-pool argument in the abstract. This
> doc makes it concrete for *this* structure: how a `Node*` becomes a `page_id`, what a node
> looks like as bytes, how you avoid reading the whole tree to answer one query, and what
> "durable" actually requires.
>
> **You do not need all of this.** §9 gives the honest cut-down: for an immutable segment
> index — which is what your engine actually builds — you can skip the buffer pool, skip the
> WAL, and skip in-place updates entirely. Read the whole doc, then decide deliberately.

---

## 1. The one change that drives everything

```cpp
struct Node { Node* children[F]; };        // in memory
struct Page { uint32_t children[F]; };     // on disk — page IDs, not addresses
```

A `Node*` is a virtual address: process-local, run-local, meaningless in a file. Replace it
with a **page ID** — an index into the file, where `byte_offset = page_id × PAGE_SIZE`
(frontier doc 02 §3). Fixed-size pages make that translation pure arithmetic; that's *the*
reason pages are fixed size.

Every consequence in this doc follows from that substitution:

- **Dereferencing is no longer free.** `children[i]` used to be a load; now it's "find or
  read page 4711," which may be a syscall. So you need a **page table / buffer pool** (§3).
- **A page must be self-describing.** Nothing outside it says whether it's a leaf. So it
  carries a header (§2).
- **You must decide when bytes hit the platter.** Which means `fsync`, ordering, and torn
  writes (§6).
- **Page 0 must be findable.** Something has to say "the root is page N" (§2.4).

> **Convention: reserve page ID 0 as the null pointer**, and put the file header there. Then
> `page_id == 0` means "no child," exactly like `nullptr`, and you never have to store a
> separate validity flag.

---

## 2. The page format

Design the bytes first, on paper. Once written to a file, the format is a compatibility
commitment — the same lesson as `startup-02`'s serialization discussion.

### 2.1 Common header (16 bytes)

```
 offset  size  field
 ──────────────────────────────────────────────────────────────────
   0      1    page_type      1 = internal, 2 = leaf, 3 = file header, 4 = overflow
   1      1    flags          reserved
   2      2    count          number of KEYS in use
   4      4    checksum       CRC32 of bytes [8, PAGE_SIZE)
   8      4    next_page      leaf only: right sibling; 0 = end of chain     (I7)
  12      4    reserved       future use — keep the header 16-byte aligned
 ──────────────────────────────────────────────────────────────────
  16 .. PAGE_SIZE : payload
```

- **`checksum` is not optional.** Disks lie: bit rot, torn writes, misdirected writes. Lucene
  checksums every file. Verify on read in debug builds and after any crash recovery. Without
  it, corruption presents as an inexplicable logic bug months later.
- **`count` is keys**, not children. Children are `count + 1` (I2) — don't store a redundant
  field that can disagree with the truth.
- **Keep the header small.** 16 bytes out of 4096 is 0.4%; 64 bytes would cost you ~2 keys of
  fanout per node.

### 2.2 Internal page payload — fixed-size keys

```
 [ key₀ ][ key₁ ] ... [ key_{count-1} ]        count × KEY_SIZE
 [ child₀ ][ child₁ ] ... [ child_count ]      (count+1) × 4 bytes
```

Keys first, then children: the search touches **only keys**, so keeping them contiguous means
a binary search stays within the first few cache lines of the page. Interleaving
`key,child,key,child` would double the lines touched. (Same argument as doc 02 §3's parallel
arrays — it applies at every level of the hierarchy.)

### 2.3 Leaf page payload — variable-length keys need a slot directory

Terms are variable length, so you can't index by multiplication. Use the standard **slotted
page** (frontier doc 02 §5):

```
 ┌────────────────────────────────────────────────────────────────┐
 │ header (16 B)                                                   │
 ├────────────────────────────────────────────────────────────────┤
 │ slot₀ │ slot₁ │ slot₂ │ ...        →  grows RIGHT               │
 │   (each slot: uint16 offset, uint16 length)                     │
 ├────────────────────────────────────────────────────────────────┤
 │                    ← free space →                               │
 ├────────────────────────────────────────────────────────────────┤
 │   ... ← grows LEFT ...  │ entry₂ │ entry₁ │ entry₀ │            │
 └────────────────────────────────────────────────────────────────┘
   entry = [keyLen varint][key bytes][value bytes]
```

- **Slots are fixed size and sorted by key**, so binary search works on the slot array even
  though the entries themselves are scattered in the page. Insert = shift slots (cheap, small)
  and append the entry at the low-water mark (no shifting of entry bytes).
- **Two-way growth** puts all free space in one contiguous middle region — the classic layout,
  and it makes "does this fit?" a single subtraction.
- **Free space is `slotEnd − entryStart`.** A leaf is "full" when the next entry doesn't fit,
  not when it hits a count. **So `LEAF_MAX` becomes byte-based**, and the split point in doc
  04 §3 becomes "split at the entry where cumulative bytes cross half the payload" rather
  than `total / 2`. Same algorithm, different fullness metric. Note that the "both halves meet
  the minimum" proof from doc 04 §3 no longer holds automatically — with byte-based fullness,
  a single huge entry can leave one half nearly empty. Handle oversized values with an
  **overflow page** (page_type 4) storing a `(page_id, length)` pointer in the leaf instead
  of the value.
- Deletion leaves gaps; **compact** the page when free space is fragmented and a new entry
  doesn't fit despite sufficient total free bytes.

### 2.4 The file header (page 0) — the commit point

```
 magic ("BPT1")  |  page_size  |  root_page_id  |  height  |  entry_count
 free_list_head  |  next_page_id  |  format_version  |  checksum
```

This is the **root of the whole structure**, exactly analogous to Lucene's `segments_N`
(frontier doc 02 §4). Everything else is reachable from it, and *updating `root_page_id` is
what makes a batch of changes visible*. Which makes it the natural atomic commit point (§6).

---

## 3. The buffer pool — you cannot avoid it

You cannot hold the file in memory (that defeats the purpose) and you cannot re-read a page
per access (far too slow). So: a cache of pages, with pinning and eviction.

```cpp
class BufferPool {
public:
    Page* fetch(uint32_t pageId);          // pin + return; reads from disk on miss
    void  unpin(uint32_t pageId, bool dirty);
    uint32_t allocate();                   // new page: free list, else extend the file
    void  free(uint32_t pageId);           // push onto the free list
    void  flushAll();                      // write all dirty pages

private:
    struct Frame { Page page; uint32_t pageId; int pinCount; bool dirty; };
    std::vector<Frame>                       frames_;
    std::unordered_map<uint32_t, std::size_t> pageTable_;   // pageId → frame index
    // + an eviction policy: LRU, CLOCK, or LRU-K
};
```

**Pinning is the critical discipline.** A pinned page cannot be evicted. While you hold a
`Page*` you *must* be pinned, or the pool may evict and reuse the frame under you — a
use-after-free with no sanitizer to catch it, because the memory is still validly owned by
the pool. This is exactly the borrowed-reference problem from doc 06 §4, with worse
consequences.

Use RAII, which is the same ownership discipline as your `ownership-and-lifecycle-*` docs:

```cpp
class PinnedPage {
    BufferPool* pool_; uint32_t id_; Page* page_; bool dirty_ = false;
public:
    PinnedPage(BufferPool& p, uint32_t id) : pool_(&p), id_(id), page_(p.fetch(id)) {}
    ~PinnedPage() { if (pool_) pool_->unpin(id_, dirty_); }
    PinnedPage(PinnedPage&&) noexcept;  PinnedPage& operator=(PinnedPage&&) noexcept;
    PinnedPage(const PinnedPage&) = delete;                 // pins must not be copied
    Page* operator->() { dirty_ = true; return page_; }     // mutable access marks dirty
    const Page* operator->() const { return page_; }
};
```

**Now doc 04's insert must become iterative** (doc 04 §6.1's path-stack form). The recursive
version holds a `Page*` in every stack frame, pinning `h` pages for the whole call — which is
fine at `h = 4`, but the unwind order matters for correctness and you want it explicit. The
path stack becomes a `std::vector<PinnedPage>`, unpinned in reverse as you unwind. **This is
the concrete reason doc 02 §7.3 recommended a path stack over parent pointers** — a parent
pointer would be a page ID you'd have to fetch and pin *again* on the way up, doubling your
I/O.

### Eviction policy

- **LRU** — fine, and what you should build. A `list` + `unordered_map`, ~50 lines.
- **CLOCK** — approximates LRU with one reference bit per frame and a rotating hand. Cheaper
  (no list surgery per access) and what most real systems use.
- **The B+Tree-specific insight:** internal pages are touched on *every* query and leaves are
  touched once each. Plain LRU handles this correctly by accident — the root and level-1 pages
  are always recently used. But a large **scan** touches thousands of leaves once and evicts
  your entire hot set. That's *sequential flooding*, and the standard fix is to fetch scan
  pages with a hint that puts them at the LRU tail rather than the head. Worth knowing about
  before your first slow-scan bug report.

> **The alternative: mmap.** Map the file and let the OS page cache be your buffer pool.
> Pointers become real pointers into the mapping and the whole class above disappears. Frontier
> doc 02 covers the trade properly; the short version is that mmap is a very good fit for
> **immutable, read-mostly** files — which is exactly the segment case in §9 — and a poor fit
> for a mutable tree, because you lose control over write ordering, which is what §6 needs.
> On Windows: `CreateFileMapping` + `MapViewOfFile`.

---

## 4. Serialization: node ⇄ page

```cpp
void serializeInternal(const Node& n, Page& p) {
    p.header.page_type = PAGE_INTERNAL;
    p.header.count     = (uint16_t)n.keys.size();
    uint8_t* out = p.payload;
    for (const Key& k : n.keys) out += writeKey(out, k);           // fixed or varint
    for (uint32_t c : n.childPageIds) out += writeU32LE(out, c);   // LITTLE-ENDIAN, always
    p.header.checksum = crc32(p.bytes + 8, PAGE_SIZE - 8);
}
```

Four rules, all of which are format commitments you can't take back:

1. **Fixed-width, explicit-endian integers.** `writeU32LE`, never `memcpy(&n.count, ...)`.
   Structure layout and endianness differ across compilers and machines; a file written by
   MSVC must be readable by MinGW. Pick little-endian (matches x86/ARM, no conversion in
   practice).
2. **Version the format** in the file header. Frontier doc 02 §4's `.meta`. The first time you
   need to change the page layout, a version field is the difference between a migration and
   a rewrite.
3. **Never write raw pointers.** Enforce it structurally: give the on-disk node type a
   `uint32_t children[]` so a `Node*` can't be written by accident. Type systems beat
   discipline.
4. **Checksum last, over everything after it.** Compute after all payload writes; verify
   immediately on read.

**Pointer swizzling** is the name for translating page IDs → in-memory pointers on load and
back on write. You have two options:

- **Swizzle eagerly** — on page load, replace IDs with real pointers to pinned frames. Fast
  traversal, but every referenced page must stay pinned. Complex.
- **Don't swizzle** — keep IDs and call `pool.fetch(id)` at each step. One hash lookup per
  level (~20 ns) versus a DRAM miss (~80 ns) or a disk read (~100 µs). **Do this.** The hash
  lookup is noise, and the code stays simple. Optimise only if a profile says otherwise.

---

## 5. What changes in the algorithms

Reassuringly little. The algorithms from docs 03–05 are **unchanged**; only the accessors are.

| In-memory | On-disk |
|---|---|
| `Node* child = node->children[i]` | `PinnedPage child(pool, node->childPageIds[i])` |
| `new Node()` | `pool.allocate()` → a fresh page ID |
| `delete node` | `pool.free(pageId)` → push to the free list |
| `leaf->next = right` | `leafPage->header.next_page = rightPageId` |
| Node is "full" at `count > MAX` | Page is full when the **next entry doesn't fit in bytes** |
| Split moves vector elements | Split writes two pages and allocates one ID |
| Root changes → assign `root_` | Root changes → **update page 0 and fsync** (§6) |

Two genuinely new concerns:

**Free-list management.** Freed pages (from merges) must be reusable, or your file grows
forever. A singly-linked free list works: `free_list_head` in the file header, and each free
page stores the next free page ID in its first 4 bytes. `allocate()` pops, `free()` pushes.
Zero extra storage.

**Byte-based fullness.** As noted in §2.3, this changes the split point and weakens doc 04
§3's minimum-occupancy proof. Enforce a *byte* minimum (e.g. a page must be ≥ 40% full after
a split) and handle the pathological single-huge-entry case with overflow pages.

---

## 6. Durability — what "saved" actually means

`write()` returns and the data is **not** on the platter — it's in the OS page cache. A power
loss loses it. Worse, the disk may write your 4096-byte page **partially** (a *torn write*),
leaving a page that is neither the old nor the new version. Your checksum detects that but
cannot repair it.

Three levels of durability. Pick one deliberately and write down which.

### Level 1 — none (rebuild on crash)

Write when convenient, `fsync` on clean shutdown. On crash, discard and rebuild from source.
**For a search index this is often exactly right** — the source documents are the truth, and
the index is derived. Frontier doc 03's "commit point" model plus a rebuild path. Cost: a
slow restart. Zero implementation.

### Level 2 — atomic root swap (copy-on-write)

Never modify a page in place. Write modified pages to **new** page IDs, then update the root
pointer:

```
 1. write every modified page to NEW page IDs      (old pages still intact)
 2. fsync the data pages                            ← ensures they're durable BEFORE step 4
 3. write a new file header with the new root_page_id
 4. fsync the header                                ← THE COMMIT POINT
```

Because step 3 writes a single 4-byte field inside one sector, and sector writes are atomic on
essentially all real hardware, the transition is all-or-nothing: **either you see the old root
or the new one, never a mix.** A crash between 2 and 4 leaves the old tree perfectly intact,
with some orphaned pages to garbage-collect.

**The `fsync` between 2 and 3 is load-bearing.** Without it the OS may reorder, landing the new
header before the pages it points at — and now your root points at garbage. This is the same
happens-before reasoning as your `startup-04-completion-signalling` doc, applied to a disk.

**The cost is write amplification:** changing one leaf rewrites the whole root-to-leaf path
(~4 pages). The benefit is that you get **snapshots and MVCC almost free** — old roots remain
valid trees, so a reader holding an old root ID sees a consistent point-in-time view while a
writer proceeds. LMDB is built exactly this way. **This is the sweet spot for you**: modest
complexity, real durability, and snapshot reads.

### Level 3 — write-ahead log

Append `(pageId, before, after)` records to a log, `fsync` the log, then modify pages in place
at leisure. On restart, replay. This is what InnoDB and Postgres do. It gives the best
steady-state write throughput (sequential log writes, batched page writes) at a large
complexity cost: log format, checkpointing, recovery, log truncation. **Don't build this
unless you've measured that Level 2's write amplification is your bottleneck.**

---

## 7. The term dictionary — the actual destination

Tying this back to `searchengine`. Frontier doc 02 §3: *"the term dictionary stores, per
term, the byte offset of its posting list."*

```
  Key   = std::string       the term
  Value = struct { uint64_t postingsOffset; uint32_t postingsLength; uint32_t docFreq; }
```

Query path:

```
  "laptop"
     │
     ▼  B+Tree lookup in _0.terms   → 3 page reads, 2 of them cached
  { postingsOffset = 148_302, length = 4_096, docFreq = 1_204 }
     │
     ▼  seek + read in _0.postings
  the compressed posting list  →  frontier doc 04
```

Prefix query `"lap*"` (frontier doc 05's "query rewriting"):

```
  scanPrefix("lap")  →  one descent + a sequential leaf walk (doc 03 §6)
                     →  every matching term with its postings offset, in order
                     →  union the posting lists
```

**Design notes specific to this use:**

- **The segment index is immutable** (frontier doc 03). Written once by a flush or merge, then
  read-only forever. So: **bulk load it** (doc 07 §8) — `O(N)`, 100% occupancy, leaves
  physically sequential — and **never implement insert or delete on the disk version at all.**
  This deletes most of this doc's complexity. See §9.
- **Suffix truncation (doc 07 §7.1) matters a lot here.** Shorter separators → higher internal
  fanout → the internal levels of a multi-million-term dictionary fit in a few hundred KB and
  stay resident. Combined with prefix compression in leaves, this is why real term
  dictionaries are so small.
- **Lucene uses an FST, not a B+Tree**, for the term index — smaller still, and it supports
  fuzzy/regex automaton intersection. That's a genuinely better structure for this specific
  job, and it's much harder. **A B+Tree first is the right call**: you'll understand the
  problem the FST solves, and you'll have a working engine while you learn it.

---

## 8. Windows specifics

Your platform, from `cpp-file-io.md`:

| Need | POSIX | Windows |
|---|---|---|
| Open | `open()` | `CreateFileW` |
| Positional read | `pread()` | `ReadFile` + `OVERLAPPED.Offset` |
| Force to disk | `fsync()` | `FlushFileBuffers()` |
| Bypass OS cache | `O_DIRECT` | `FILE_FLAG_NO_BUFFERING` (requires sector-aligned buffers **and** sector-sized reads) |
| Memory map | `mmap` | `CreateFileMapping` + `MapViewOfFile` |
| Preallocate | `posix_fallocate` | `SetFileValidData` / `SetEndOfFile` |

Three traps:

1. **Open in binary mode.** `std::fstream` without `std::ios::binary` translates `\n` → `\r\n`
   on Windows, silently corrupting any page containing byte `0x0A`. This will happen and it
   will be baffling.
2. **`FlushFileBuffers` on the file handle is required** for the §6 ordering guarantee.
   `fflush` only flushes the CRT's userspace buffer to the OS — not the OS to the disk.
   They're different layers and only one of them is durability.
3. **Sector alignment for unbuffered I/O** — `FILE_FLAG_NO_BUFFERING` needs buffers aligned to
   the physical sector size (`VirtualAlloc`, not `malloc`). If you're using a buffer pool you
   arguably want this (you're already caching; double-caching wastes RAM), but get it working
   buffered first.

---

## 9. The honest cut-down for your engine

Given that your segment indexes are **write-once, read-many**, here is what you actually need
— and it's a fraction of this doc:

```
 ✓ page format + checksums                       §2       (~150 lines)
 ✓ bulk loader writing pages sequentially        doc 07 §8 (~80 lines, already written)
 ✓ read path: fetch page, binary search, descend §5       (~100 lines)
 ✓ mmap the file, let the OS page cache handle it §3 note (~40 lines)
 ✓ file header with root page id + version       §2.4     (~30 lines)

 ✗ buffer pool with pinning/eviction   — the OS page cache does this for an immutable file
 ✗ insert / delete on disk             — segments are immutable; rebuild instead
 ✗ free list                           — nothing is ever freed within a segment
 ✗ WAL                                 — nothing is ever modified
 ✗ copy-on-write root swap             — the file is written once, then renamed atomically
```

**~400 lines instead of ~2000.** You get durable, memory-mapped, prefix-scannable term
dictionaries with 100% occupancy and sequential layout — and the immutability that makes it
all simple is the *same* design commitment frontier doc 03 already made for you at the
segment level.

> **Build the mutable version only when you have a use case that actually mutates in place.**
> Right now you don't: your write path is "buffer in memory → flush a new immutable segment →
> merge segments in the background." That's an LSM-tree shape, and in an LSM the B+Tree's role
> is the **read-only sorted file**, which is the easy half. Recognising that you don't need the
> hard half is the engineering call here.

---

## 10. Checkpoint before doc 09

1. Why must on-disk children be page IDs rather than pointers? Give two independent reasons. (§1)
2. Why do internal pages store all keys before all child IDs? (§2.2)
3. What is a slot directory for, and why do slots and entries grow toward each other? (§2.3)
4. What breaks if you hold a `Page*` without pinning it? Why won't ASan catch it? (§3)
5. In the Level-2 commit sequence, why is the `fsync` between steps 2 and 3 load-bearing? (§6)
6. Why does copy-on-write give you snapshot reads almost for free? (§6)
7. Your segment index is immutable. Name four components from this doc you can delete. (§9)

**Build now — but scoped:** implement §9's cut-down, not this whole doc. Concretely: define
the page format as a header with `static_assert(sizeof(PageHeader) == 16)`; write
`bulkLoadToFile(sortedTerms, path)` using doc 07 §8's algorithm but emitting pages; write
`DiskBPlusTree` with mmap-backed `find()` and `scanPrefix()`. Test it by bulk loading 100,000
terms, closing, reopening, and verifying that every term is findable and that prefix scans
match the in-memory tree's results **exactly**. That last cross-check — disk tree versus
in-memory tree, same queries, identical answers — is the strongest test you can write, because
it validates the format, the reader, and the writer against a reference you already trust.
