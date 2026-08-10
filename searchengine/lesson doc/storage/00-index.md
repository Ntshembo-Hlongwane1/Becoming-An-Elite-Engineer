# Storage Engine in C++ — Page, DiskManager, BufferPool, and a Disk-Backed B+Tree

> **Goal:** finish this series with `BPlusTree` running entirely on 4 KB pages read through a
> `BufferPool` from a real file on disk — inserting, searching, splitting, merging, surviving
> process restart — and understand, at the byte and syscall level, *why every line is there*.
>
> **This is the hands-on series.** `lesson doc/bplustree/08-disk-and-persistence.md` sketched
> the design; this one builds it. Every component is given as full working code with
> line-by-line commentary, and every doc ends with a program you write and run before moving
> on. If you do all twelve checkpoints in order, you cannot end up without a working engine —
> each one is a runnable proof that the layer beneath it is correct.
>
> **Second goal, equally serious: low-latency instinct.** You are not just persisting data,
> you are learning where time actually goes. Docs 01 and 12 are measurement labs. Several
> "optimisations" in this series will *not* help on your machine — the point is that you will
> know, because you measured, instead of believing.

---

## What you already have

You built a correct in-memory B+Tree in
`internal/kernal/core/datastructures/bplustree/BPlusTree.hpp`:

- descent by `UpperBoundIndex`, leaf lookup by `LowerBoundIndex`
- leaf split (copy separator up) and internal split (move separator up)
- delete with borrow-from-sibling, merge, and root shrink
- `Validate()` checking depth uniformity, fanout, occupancy, separator windows, parent
  pointers, and leaf-chain order

**That code is not thrown away — it becomes your oracle.** The disk version must produce the
same answers for the same operations, and doc 11 wires both up to the same stress test so any
divergence is caught immediately. Keep the in-memory tree exactly as it is.

---

## The one sentence that drives all twelve docs

> A pointer is a promise about *this process's address space*, and that promise expires the
> moment the process does. On disk there are no pointers — only **page numbers**, and a
> **cache** that turns a page number back into a pointer, temporarily, under your control.

Everything below is a consequence of that sentence.

---

## The architecture you are building

```
            BPlusTree                      "give me page 42"
                 |                                 |
                 v                                 v
           PageGuard  (RAII: pins on construct, unpins on destruct)
                 |
                 v
            BufferPool         frames[]  +  page_table  +  LRUReplacer
                 |             ^                                    |
       miss ->   |             +--- hit: return frame, no I/O ------+
                 v
           DiskManager         ReadPage(id, buf) / WritePage(id, buf) / Allocate / Free
                 |
                 v
           searchengine.db     [ page 0: header | page 1 | page 2 | ... ]
                                        4096 bytes each
```

Read it bottom-up: the file is an array of 4 KB slots. `DiskManager` does offset arithmetic
and syscalls, nothing else. `BufferPool` keeps a fixed number of those slots in RAM and
decides which to evict. `PageGuard` makes it impossible to forget to release one. `BPlusTree`
sees only page ids and guards.

---

## The series

| Doc | Title | You will have built |
|---|---|---|
| [01](01-the-cost-model.md) | The Cost Model | A benchmark proving why 4096, why sequential ≠ random, what a syscall costs |
| [02](02-the-page.md) | The Page | `Page`, `page_id_t`, `PAGE_SIZE` — and why it is a raw byte array |
| [03](03-diskmanager-io.md) | DiskManager I — Raw I/O | `ReadPage` / `WritePage` / `Sync` against a real file |
| [04](04-diskmanager-allocation.md) | DiskManager II — Allocation | Page 0 header, `AllocatePage`, `DeallocatePage`, the on-disk free list |
| [05](05-node-page-layout.md) | Node Page Layout | Node header + key array packed into 4096 bytes; fanout derived, not chosen |
| [06](06-bufferpool-core.md) | BufferPool I — Frames | Page table, pin counts, dirty flags, `FetchPage` / `NewPage` / `UnpinPage` |
| [07](07-bufferpool-eviction.md) | BufferPool II — Eviction | O(1) LRU replacer, the eviction path, write-back correctness |
| [08](08-page-guards.md) | PageGuard | RAII + move semantics; why a leaked pin deadlocks the pool |
| [09](09-btree-descent-on-pages.md) | B+Tree I — Descent | `FindLeaf` over page ids; `Search`, `Contains`, `RangeSearch` on disk |
| [10](10-btree-insert-on-pages.md) | B+Tree II — Insert & Split | Splits that allocate pages; root growth; sibling links across pages |
| [11](11-btree-delete-and-durability.md) | B+Tree III — Delete & Durability | Merge/borrow freeing pages; `fsync`; kill -9 and reopen |
| [12](12-latency-lab.md) | The Latency Lab | `pread`/`pwrite`, cache lines, branch cost, and how to measure honestly |

---

## Design decisions, made up front

These are fixed for the whole series so the code compounds instead of churning. Each is
justified where it first appears.

| Decision | Value | Where justified |
|---|---|---|
| Page size | **4096 bytes** | 01 §4 — OS page, NVMe block, and mmap granularity all agree |
| Page id | `uint32_t`, page 0 = file header | 02 §3 — 4 G pages × 4 KB = 16 TB, plenty |
| Key type | **fixed-size `uint64_t`** first | 05 §2 — variable-length keys via slot directory in 05 §8 |
| Leaf value | `PostingRef { uint64 offset; uint32 length; }` | 05 §3 — the search-engine mapping |
| File I/O | `<fstream>` first, native `pread`/`pwrite` in doc 12 | 03 §2 — portable now, fast later, measured in between |
| Eviction | **LRU**, O(1) via list + hash | 07 §2 — with Clock and LRU-K compared |
| Concurrency | **single-threaded** | 11 §7 — latch crabbing described, not built |

> **On the key type.** Your in-memory tree is `template<typename KeyType>`. The disk version
> is *not* templated on an arbitrary key, and doc 05 §1 explains why at length: a template
> parameter is a compile-time promise about a type's *behaviour*, but a page format is a
> runtime promise about a type's *bytes*. `std::string` has a pointer inside it. You cannot
> `memcpy` a promise.

---

## The C++ primitives, and where each is explained

Every language construct is explained **once, at its first use**, and assumed thereafter. If a
later doc uses it differently in a way that changes behaviour, the difference is called out
there and points back here. Use this as a lookup when something reappears and you want the
original explanation.

| Construct | Explained in | The question it answers |
|---|---|---|
| `auto`, lambdas & capture modes | [01](01-the-cost-model.md) §6.1 | What is `[&]` capturing, and when does it dangle? |
| templates vs `std::function` | 01 §6.1 | Why a template for the benchmark callable? |
| forwarding reference `F&&` | 01 §6.1 | Why is this *not* an rvalue reference? |
| defeating the optimiser | 01 §6.1 | Why does the benchmark accumulate a checksum? |
| `#pragma once` | [02](02-the-page.md) §1 | Versus include guards; why it's faster |
| `using` type aliases | 02 §1 | Why it gives intent but not safety |
| **`inline constexpr`** | 02 §1 | Two keywords, two jobs — ODR, linkage, and a measured demo |
| `std::byte`, `enum class` | 02 §2.1 | Scoping, no implicit int, fixing the underlying type |
| **alignment, `alignas`/`alignof`** | 02 §2.3 | What alignment *is*; over-alignment; C++17 aligned `new` |
| `static_assert` vs `assert` | 02 §2.4 | Compile-time vs runtime; the `NDEBUG` trap |
| strict aliasing, the `memcpy` idiom | 02 §4 | Why a pointer cast is UB and `memcpy` is not |
| what `memcpy` compiles to | 02 §4.1 | Verified assembly: one instruction |
| **the five casts** | 02 §4.2 | `static_` vs `reinterpret_` vs `dynamic_` vs C-style |
| type traits, the `_v` suffix | 02 §5 | What "trivially copyable" licenses you to do |
| integer promotion & overflow | [03](03-diskmanager-io.md) §4 | Measured: which multiply overflows and which doesn't |
| `throw`, stack unwinding | 03 §8 | Zero-cost when not thrown; why destructors can't throw |
| `explicit` | 03 §9.1 | The file that gets opened by a conversion nobody wrote |
| `= delete` | 03 §9.1 | Versus private; why an owning class can't be copied |
| `const` member functions | 03 §9.1 | What it protects — and what it doesn't |
| `static` free functions | 03 §9.1 | Internal linkage (meaning 1 of 3) |
| `#if defined` / macros | 03 §9.1 | The branch your compiler never type-checks |
| **struct layout & padding** | [04](04-diskmanager-allocation.md) §2 | Why `sizeof` isn't the sum of the members; `#pragma pack` |
| generic lambdas | 04 §3 | `auto` parameters; why it compiles to nothing |
| **reference data members** | [05](05-node-page-layout.md) §6 | Non-owning views; why `operator=` disappears |
| `static` member functions | 05 §6 | Meaning 2 of 3 |
| default member initialisers, `{}` | [06](06-bufferpool-core.md) §3 | One brace between a zeroed header and stack garbage |
| `unique_ptr<T[]>` | 06 §4.1 | Why `[]` in the type, and `delete[]` vs `delete` |
| **container invalidation rules** | 06 §4.2 | The table that decides whether a design is possible |
| **arenas** | 06 §4.3 | What you built without naming it; bump vs pool vs slab |
| `unordered_map`, `find`/`end` | 06 §5 | Why `map[k]` is not a lookup |
| `std::list` and node containers | [07](07-bufferpool-eviction.md) §3 | Why it's usually wrong, and why it's right here |
| **rvalue refs, `std::move`** | [08](08-page-guards.md) §3.1 | `std::move` moves nothing — it's a cast |
| **rule of 0/3/5**, `= default` | 08 §3.1 | Why a destructor silently kills your move operations |
| `noexcept` | 08 §4.4 | Why `vector` refuses to move without it |
| **copy elision / NRVO** | [09](09-btree-descent-on-pages.md) §5 | Why `return guard;` compiles on a move-only type — and why `return std::move(g);` is worse |
| default arguments | 09 §5 | Pointer-with-null vs reference, revisited |
| **the unsigned reverse-loop trap** | [10](10-btree-insert-on-pages.md) §3 | Verified infinite loop that `-Wall -Wextra` does not catch |
| sink parameters | 10 §3 | Taking ownership in a signature |
| branch prediction, `CMOV` | [12](12-latency-lab.md) §3 | What a pipeline flush costs, and when branchless loses |
| `__builtin_prefetch` | 12 §4 | A hint, not a load; why timing is everything |
| **`inline` is not about speed** | 12 §8.1 | The keyword's real meaning, and `-flto` |

The bolded rows are the ones that change how you write C++ generally, not just here.

## Prerequisites

- **Your working `BPlusTree.hpp`** — the oracle. Do not modify it.
- `lesson doc/bplustree/07-efficiency-and-layout.md` §1 — the fanout arithmetic; doc 05 here
  is the byte-exact version of it.
- `lesson doc/bplustree/08-disk-and-persistence.md` — the design sketch. This series is its
  implementation, and where they disagree, **this series wins** (it is the one that compiles).
- `lesson doc/frontier/01-memory-hierarchy.md` — doc 01 here assumes you have seen the
  latency table before and goes straight to measuring it on *your* machine.

## Where this lands in `searchengine`

```
internal/kernal/core/storage/
    Page.hpp                 <- doc 02
    DiskManager.hpp/.cpp     <- docs 03, 04
    BufferPool.hpp/.cpp      <- docs 06, 07
    PageGuard.hpp            <- doc 08
    NodePage.hpp             <- doc 05
internal/kernal/core/datastructures/bplustree/
    BPlusTree.hpp            <- untouched, your oracle
    DiskBPlusTree.hpp/.cpp   <- docs 09, 10, 11
internal/kernal/core/storage/tests/
    <one test file per checkpoint>
```

`storage/` is a new sibling to `datastructures/` — deliberately, because a buffer pool is not
a data structure and the boundary between "manages bytes on disk" and "arranges keys in order"
is the single most important seam in the whole engine. Doc 09 §1 is about respecting it.

## How to read

**Front to back, writing code as you go.** Do not read ahead — doc 06 will not make sense
until you have felt the problem doc 05 leaves you with.

Every doc ends with a **Checkpoint**: a small program you write and run. The checkpoint is not
optional and it is not a quiz — it is the regression test for the layer you just built, and
doc 11 runs all twelve of them together. If a checkpoint fails, do not continue; the bug is
cheaper to find now than three layers up.
