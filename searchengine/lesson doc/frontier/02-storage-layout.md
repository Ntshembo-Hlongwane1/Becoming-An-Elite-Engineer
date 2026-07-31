# 02 — Storage Layout

> **From doc 01 you know the atom is the page.** This doc turns that into concrete file
> format: how bytes are grouped into pages, how you *address* data on disk (page IDs and
> offsets — pointers that aren't memory pointers), how metadata ties files together, and the
> central plumbing decision — **`read()` + your own buffer pool** vs **`mmap` + the OS page
> cache** — with the immutability argument that decides it for a search index. Includes
> Windows specifics, since that's your platform.

---

## 1. The layering: bytes → pages → files → segments → index

Build the picture bottom-up. Every layer is "an array of the layer below."

```
 index/                         ← a directory
   ├── segments_3                ← the COMMIT POINT: lists which segments are live (doc 03)
   ├── _0.terms                  ┐
   ├── _0.postings               │ segment _0 : a set of files, each a concern
   ├── _0.points                 │ (immutable once written)
   ├── _0.docvalues              │
   ├── _0.stored                 │
   ├── _0.meta                   ┘
   ├── _1.terms ...              ← segment _1 : another self-contained inverted index
   └── ...

 one file  = a logical array of fixed-size PAGES
 one page  = a fixed-size array of BYTES (e.g. 4 KB)
 one byte  = addressable unit
```

Two design commitments here that you should copy:

1. **One file per concern.** Terms, postings, numeric points, doc values, stored fields each
   get their *own* file. Why: doc 01 §7 — different files have different *temperature*. The
   OS page cache will naturally keep the hot files (terms, skip data) resident and let cold
   files (stored documents) stay on disk. Mixing them in one file defeats that.
2. **Fixed-size pages inside each file.** So you can address any page by arithmetic (§3),
   align to OS pages and disk blocks, and cache/evict at page granularity.

> **Contrast with your current engine:** today your "index" is a live
> `unordered_map<string, vector<string>>` in RAM — no pages, no files, no addressing. The
> jump you're making is: *give the index a byte-exact on-disk representation you can address
> without loading all of it.* That representation is what this doc is about.

---

## 2. Anatomy of a page

A page isn't just 4 KB of payload; it's usually **self-describing**. A common shape:

```
 ┌────────────────────────────────────────────────────────────┐  ← one 4 KB page
 │ HEADER: page_type | entry_count | free_offset | checksum ...│  (fixed size)
 ├────────────────────────────────────────────────────────────┤
 │ payload ...                                                 │
 │ (postings block, or B+Tree node, or slotted records)       │
 │                                                             │
 └────────────────────────────────────────────────────────────┘
```

- **`page_type`** lets one file hold different page kinds (interior vs leaf node, data vs
  overflow) and lets tools validate what they're reading.
- **`checksum`** (e.g. CRC32) detects corruption — the disk *lies* sometimes (bit rot, torn
  writes). Lucene checksums every file; on open it can verify integrity. This is the
  low-level version of the "exists ≠ valid" lesson from your `startup-02` doc.
- **`free_offset` / slot directory`** — see §5 (slotted pages) for variable-length records.

> **Your turn:** decide your page size. Match the OS page (4 KB) as a default — it aligns
> your I/O with the page-cache granularity so one logical page = one page-cache page = one
> page fault. Bigger pages (16–64 KB) amortize per-page overhead for scan-heavy data but
> waste cache on point lookups. This is a real trade; pick 4 KB first, measure later.

---

## 3. Addressing: page IDs and offsets are *on-disk pointers*

In RAM you point at data with a memory address (`T*`). On disk you can't — addresses aren't
stable across runs, and the data isn't in your address space yet. So on-disk structures
point with **(file, page_id, in-page offset)** or a single **byte offset from the start of
the file**. These are the disk equivalent of pointers, and they're the crux of storage
layout.

```
 A B+Tree interior node in RAM:        On disk:
   node->child[i]  (a T* pointer)  →     child_page_id[i]  (a uint32/uint64)

 To follow it:
   RAM:   deref pointer                (nanoseconds)
   Disk:  page_start = page_id * PAGE_SIZE; read that page  (µs, maybe a fault)
```

Because pages are **fixed size**, translation is pure arithmetic:
`byte_offset = page_id * PAGE_SIZE + in_page_offset`. That O(1), branch-free translation is
*the* reason pages are fixed size. (Variable-size pages would need a lookup table just to
find a page — a self-defeating indirection.)

Key consequences you'll implement around:

- **Serialization means turning pointers into IDs** (and back on load). When you write a
  tree node, you don't write the `T*`; you write the child's `page_id`. This is
  "pointer swizzling / unswizzling" and it's the heart of persisting a pointer-based
  structure. Your `startup-02` serialization discussion was the string-format warm-up; this
  is the pointer version.
- **Offsets are typically little-endian, fixed-width** so any run can read them (§7).
- **The term dictionary stores, per term, the byte offset of its posting list** in the
  postings file. "Look up a term" = FST/B+Tree gives you an offset = `seek`+read that
  region. That indirection (term structure → offset → postings) is the spine of doc 04/05.

---

## 4. Metadata and the commit point — how files become *an index*

Loose files aren't an index until something says "these segments, these files, this version
are the current truth." That's **metadata**:

- **Per-segment `.meta`/`.si`** — how many docs, which fields, which codec/version, file
  checksums, min/max values. Read first; tells the reader how to interpret the other files.
- **The commit point (`segments_N`)** — the *root* of the whole index: the list of live
  segments and their generation. Opening an index = read `segments_N` → for each segment
  read its meta → mmap its files. Writing a new commit = write `segments_{N+1}` and fsync.
  The **atomic switch** from `segments_N` to `segments_{N+1}` is what makes a commit
  all-or-nothing (doc 03).
- **Magic number + format version** at the head of each file — reject foreign/old files
  instantly. (Again: your `startup-02` §6 "add a version header" — this is where the pros do
  it, on *every* file.)

```
 open index:
   read segments_N ──► [ _0, _1, _2 ]        (the live set)
        │
        for each segment: read _k.meta ──► field & codec info, checksums
        │
        mmap _k.terms, _k.postings, _k.points, ...
        │
        index is now queryable   (no bulk load — files are memory-mapped lazily)
```

Notice the payoff: **opening a 50 GB index is near-instant** because you don't *read* 50 GB
— you `mmap` it and let page faults pull in only the pages queries actually touch (§6). This
directly recasts your `startup` "warm start" from "load the whole index into RAM" to "map it
and fault in the hot part" — the frontier way.

---

## 5. Slotted pages — packing variable-length records (the classic technique)

Postings, terms, and stored fields are **variable length** (a term is 3 or 30 chars; a
posting list is 2 or 2 million entries). How do you pack variable-length records into a
fixed-size page and still address them? The canonical answer is the **slotted page**:

```
 ┌───────────────────────────────────────────────────────────┐
 │ header │ slot0 | slot1 | slot2 →  (grows right)            │
 │        │ (each slot = offset+length pointing into payload) │
 │                                        ...free space...    │
 │              (payload grows left) ← recordC | recordB | recA│
 └───────────────────────────────────────────────────────────┘
   slot directory at the top grows down; records at the bottom grow up;
   free space is the gap in the middle.
```

- The **slot directory** gives each record a stable *slot number*; the slot holds the
  record's offset+length within the page. So "record #2 on page 7" is a stable address even
  though records are variable-length and may be compacted.
- Deleting a record frees its slot; periodic compaction reclaims gaps. (In an *immutable*
  segment you never delete in place — but the slotted layout still matters for how you pack
  and address records within a page at *write* time.)

You don't strictly need slotted pages for append-only, sequentially-scanned postings (you
can just stream length-prefixed blocks). But you *will* want the pattern the moment you
store addressable records (stored fields for the fetch phase, or a B+Tree of
variable-length keys). Know it; it's one of the half-dozen truly foundational storage
techniques.

---

## 6. The big decision: `read()` + buffer pool  vs  `mmap` + OS page cache

This is the plumbing choice you flagged. Two ways to get disk pages into your program:

### Path A — explicit I/O + your own buffer pool (the DBMS way)

```cpp
// pseudo:
Page* p = bufferPool.fetch(page_id);   // if not cached: pread() into pool memory, evict LRU
... use p ...
bufferPool.unpin(page_id);
```

- You call `pread()`/`ReadFile` to copy a page from the OS into **memory you manage**.
- You keep a **page table** (page_id → frame), **pin/unpin** pages in use, and **evict**
  with LRU/clock. Dirty pages are written back in a controlled order (for WAL/ACID).
- **Pros:** total control — eviction policy, prefetch, write-back ordering, and (with
  `O_DIRECT`) you bypass the OS cache to avoid double-caching. Essential when data is
  *mutable* and durability ordering matters.
- **Cons:** you build and tune all of it. It's a lot of careful code, and getting eviction /
  pinning wrong is a rich source of bugs.

### Path B — `mmap` + OS page cache (the Lucene way)

```cpp
// POSIX: void* base = mmap(nullptr, len, PROT_READ, MAP_SHARED, fd, 0);
// then just treat `base` as a byte array; the kernel faults pages in on access.
const uint8_t* postings = base + term_offset;   // page-faults in on first touch
```

- You `mmap` the file into your virtual address space. Accessing a mapped byte that isn't
  resident triggers a **page fault** (doc 01 §3); the kernel pulls that 4 KB page into the
  **OS page cache** and your access resumes. Hot pages stay cached; cold pages get evicted
  by the kernel automatically.
- **Pros:** almost no code — no page table, no pins, no eviction policy (the kernel's is
  battle-tested). Zero explicit copies into a user buffer. The page cache is **shared** and
  survives across your process's own structures. Perfect for **read-mostly, immutable**
  files.
- **Cons:** page faults are **hidden, synchronous stalls** — a "memory read" can secretly
  block on disk, which wrecks latency predictability and can stall a thread you didn't
  expect. You have little control over *when* eviction happens (`madvise`/`posix_fadvise`
  give hints, not commands). And for *mutable* data, msync ordering and durability get
  subtle. (There's a well-known critique — Crotty/Leis/Pavlo, *"Are You Sure You Want to
  Use MMAP in Your DBMS?"* — arguing mmap is a poor fit for *mutable* databases for exactly
  these reasons.)

### Why the search index picks B (and the DB picks A)

The deciding factor is **immutability** — the theme that keeps paying off:

| | Mutable DB (buffer pool, A) | Immutable search segment (mmap, B) |
|---|---|---|
| Dirty pages / write-back order | Yes — must control for WAL/ACID | **None** — segments never change |
| Eviction control needed | Yes — durability + hot-set tuning | **No** — kernel LRU is fine for read-only |
| Double-caching concern | Solved with O_DIRECT | Irrelevant — page cache *is* the cache |
| Code you write | Whole buffer manager | Basically `mmap` + pointer arithmetic |

Because a Lucene segment is **write-once, read-many, never-mutated**, all the reasons a DB
needs a hand-built buffer pool evaporate. Lucene maps the file and lets the OS page cache be
the buffer pool. **This is why the buffer pool is a DB concept and mmap+page-cache is the
search concept — and immutability is the pivot.** (You had the right instinct.)

> **Your engine, pragmatically:** you can start with **plain buffered `read()`/`ifstream`**
> (simplest, correct, portable) and *graduate* to mmap once the format is stable and you
> want the page-cache-as-buffer-pool behavior. Don't build a hand-rolled buffer pool — your
> data is immutable, so you'd be building the DB solution to a problem you don't have.

### Windows note (your platform)

You're on win32, so the calls differ from POSIX but the concepts are identical:

- `mmap`/`munmap` → **`CreateFileMapping`** + **`MapViewOfFile`** / `UnmapViewOfFile`.
- `pread` → **`ReadFile`** (with `OVERLAPPED` for positioned/async reads).
- `msync`/`fsync` → **`FlushViewOfFile`** + **`FlushFileBuffers`**.
- `madvise` → **`PrefetchVirtualMemory`** / memory-mapped file hints.
- The **OS page cache** exists on Windows too (the system file cache); the "leave RAM free
  for the cache" advice applies equally.

Consider wrapping this behind a small **`Directory`-style interface** (open/read-region/
size), exactly like Lucene's `Directory` abstraction, so the rest of your engine doesn't
care whether it's `ifstream`, `ReadFile`, or `MapViewOfFile` underneath. That's good design
*and* it lets you swap strategies without touching query code.

---

## 7. Mmap-ability: endianness, alignment, fixed-width (the sharp edges)

If you `mmap` a file and then point a `struct*` at it (or read integers directly), the
**on-disk byte layout must match what your code expects**. Three traps:

- **Endianness.** An integer written on a little-endian machine and read as raw bytes on a
  big-endian one is garbage. Pick an endianness for the format (little-endian is the common
  choice) and encode/decode explicitly, or accept non-portability. Frontier formats define
  endianness precisely.
- **Alignment.** Casting a `char*` at offset 5 to a `uint32_t*` and dereferencing is
  **undefined behavior** on strict-alignment targets and slow on others. Either lay fields
  out at natural alignment (pad the format) or read bytes and assemble integers explicitly
  (`memcpy` into a `uint32_t`, or shift-or byte by byte). Safe portable reads beat clever
  pointer casts.
- **Struct packing.** `sizeof(struct)` includes compiler padding that varies. Never assume a
  `struct` maps 1:1 onto file bytes unless you control packing (`#pragma pack`) *and*
  endianness *and* alignment. The robust path is an explicit (de)serializer per field — more
  code, zero surprises. For a learning project, prefer explicit byte reads over
  reinterpret_cast onto mapped memory until you deeply understand the hazards.

> **Systems insight:** a file format is a **contract measured in bytes**. "It worked on my
> machine" is how format bugs hide. Write the layout down (offsets, widths, endianness) as a
> spec — the byte-level cousin of the grammar you'd write in `ebnf-notation.md`.

---

## 8. Putting the layout together (the mental model to carry into doc 03/04)

```
        Query wants postings for "bread"
                    │
   term structure (FST/B+Tree)  ── gives ──►  byte offset in _k.postings
                    │
   compute page_id = offset / PAGE_SIZE
                    │
   mmap region already mapped ─► touch page ─► (resident? use it : PAGE FAULT → OS page cache)
                    │
   stream the compressed postings block sequentially (prefetch-friendly, doc 01 §2)
```

Every arrow is a storage-layout concept from this doc: term→offset (§3), page arithmetic
(§3), mmap/page-cache (§6), sequential block streaming (§1/§2). Doc 04 zooms into "the
compressed postings block"; doc 03 zooms into "which segments are even live."

---

## 9. Before you move on

1. Why is a page ID + offset the on-disk equivalent of a pointer, and why must pages be
   fixed-size for that to work?
2. Why does frontier storage split terms, postings, points, and stored fields into
   *separate files*? (Tie it to doc 01 §7 temperature.)
3. Explain the buffer-pool-vs-mmap decision in one sentence, with immutability as the pivot.
4. Give two concrete failure modes of pointing a `struct*` at mmap'd bytes, and the safe
   alternative.
5. Why can a search index *open* a 50 GB file "instantly" while a naive loader would take
   minutes?
6. On your Windows platform, what replaces `mmap` and `pread`, and why does the `Directory`
   abstraction matter?

Next: **03 — Segment Architecture**, where these files gain a lifecycle: flush, refresh,
commit, merge, tombstones, deletes, and near-real-time visibility — the immutability engine
that has been quietly explaining everything.
