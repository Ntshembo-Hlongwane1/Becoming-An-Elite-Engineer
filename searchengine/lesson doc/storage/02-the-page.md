# 02 — The Page

> **Build target:** `internal/kernal/core/storage/Page.hpp` — about forty lines, no functions
> worth the name, and every one of those lines is a decision that constrains the next ten
> docs. You will also write `page_test.cpp` proving the type is exactly the size and alignment
> you claim.
>
> **Why a whole doc for a byte array.** Because it is *not* a byte array, it is a **contract
> with the disk**. The moment you give `Page` a member that isn't raw bytes, or let its size
> drift off 4096, or allow a compiler to insert padding you didn't account for, you have
> created a class of bug that manifests as silent data corruption six docs later. Get this
> right once and it stays right.

---

## 1. The two identifiers

```cpp
#pragma once
#include <cstdint>
#include <cstddef>

// The unit of I/O for the entire engine. Justified in doc 01 §4: the CPU page, the OS page
// cache granule, the NVMe logical block, and the NTFS cluster are all 4096 bytes. Anything
// smaller costs the same; anything unaligned costs double.
inline constexpr std::size_t PAGE_SIZE = 4096;

// A page's address on disk is its index in the file. Page N lives at byte offset N * 4096.
// That is the whole addressing scheme, and it is why this type is an integer and not a
// pointer: an integer means the same thing tomorrow, in a different process, on a different
// machine. A pointer does not.
using page_id_t = std::uint32_t;

// Sentinel for "no page here". Used by the free list terminator (doc 04), by a leaf's
// next/prev links at the ends of the chain (doc 05), and by every function that can fail to
// find a page.
inline constexpr page_id_t INVALID_PAGE_ID = 0xFFFFFFFFu;
```

### Why `uint32_t` and not `uint64_t`

```
  2^32 pages  x  4096 bytes  =  16 TB
```

Sixteen terabytes in a single index file. If you exceed that you have problems this series
does not address (you want partitioning, not a wider integer).

The gain is not memory in RAM — it is **fanout**. An internal node stores one page id per
child. At 4 bytes each you fit roughly twice as many children per page as at 8 bytes, and
from doc 01 §4 you know that fanout sets the base of the logarithm that sets the height that
multiplies your 20 µs cold read. **A four-byte saving in a page format is a real latency
win**, which is the mindset the entire next doc runs on.

### Why `0xFFFFFFFF` and not `0`

Page 0 is a real, meaningful page: the file header (doc 04). If `INVALID_PAGE_ID` were 0, then
"uninitialised" and "the header" would be indistinguishable, and a zero-filled region of a
freshly-extended file would look like a valid pointer to the header. Using the maximum value
means an accidental zero is *obviously* wrong the first time you dereference it, and a
zero-filled page reads as "points at the header", which your validator can reject on sight.

> **This is a general low-level habit worth internalising:** choose sentinels that are
> *implausible as accidents*. Zero and `nullptr` are what memory looks like when nobody
> initialised it, so they make terrible "this is definitely invalid" markers.

---

## 2. The Page itself

```cpp
// The in-memory image of exactly one on-disk page. Nothing more.
//
// This type is deliberately dumb: it has no page id, no pin count, no dirty flag, no methods
// that interpret its contents. It is 4096 bytes and a promise about their alignment.
struct alignas(PAGE_SIZE) Page {
    std::byte data[PAGE_SIZE];
};

// If either of these ever fires, stop and fix it before continuing. Both are conditions the
// DiskManager's offset arithmetic silently assumes.
static_assert(sizeof(Page)  == PAGE_SIZE, "Page must be exactly one page; check for padding");
static_assert(alignof(Page) == PAGE_SIZE, "Page must be page-aligned for unbuffered I/O");
```

Four decisions in five lines. Each one below.

### 2.1 Why `std::byte` and not `char` or `unsigned char`

`std::byte` (C++17) is an enum class over `unsigned char`. It gets you two things:

1. **It is not a character type.** You cannot accidentally `std::cout << page.data` and dump
   4 KB of binary to a terminal, or pass it to `strlen`, or have it participate in integer
   promotion in an expression you didn't intend. The type says "these are bytes, not text",
   and the compiler enforces it.
2. **It keeps the aliasing exemption.** This matters enormously and is the subject of §4:
   `char`, `unsigned char`, and `std::byte` are the only types allowed to alias any other
   object. Doc 05 depends on this being legal.

What it costs: `std::byte` doesn't implicitly convert to anything, so you'll write
`reinterpret_cast<char*>(page.data)` at the `fstream` boundary in doc 03. That is a feature —
the cast is exactly where the type system hands off to a C API, and it should be visible.

### 2.2 Why a plain array and not fields

The tempting version:

```cpp
struct Page {                  // DO NOT DO THIS
    std::uint16_t entryCount;
    std::uint8_t  isLeaf;
    page_id_t     nextPage;
    std::uint64_t keys[510];
};
```

It looks self-documenting. It is a trap, for four reasons:

1. **Padding.** The compiler will insert bytes between `isLeaf` and `nextPage` to satisfy
   alignment. `sizeof` is now implementation-defined, and your `static_assert` fails — or
   worse, it happens to pass on your compiler and fails on the next one, *after* you have
   written a million pages in the old layout.
2. **One layout, but you need several.** An internal page and a leaf page have different
   contents. A header page has a third. You would need three `Page` types, and the buffer pool
   would have to know which is which — but the buffer pool's entire job is to *not care what
   is in a page*. That is the layering seam from doc 00, and this struct violates it on line 1.
3. **It bakes in the key type.** `std::uint64_t keys[510]` is a commitment made in the wrong
   file. Doc 05 derives the key count from the page size; it must not be a literal here.
4. **Endianness and portability become silent.** With raw bytes, serialisation is an explicit
   act you can audit in one place. With fields, "writing the struct" is a `memcpy` whose
   correctness depends on your compiler's ABI.

The rule: **`Page` is transport, not interpretation.** Doc 05 builds a *view* over these bytes.
The separation is what lets the buffer pool be honestly generic.

### 2.3 Why `alignas(PAGE_SIZE)`

You do not strictly need this for `fstream`. You absolutely need it for the fast path in doc 12.

Unbuffered I/O — `FILE_FLAG_NO_BUFFERING` on Windows, `O_DIRECT` on Linux — bypasses the OS
page cache entirely and DMAs straight from the device into your buffer. Because the hardware
is doing the transfer, the OS imposes hard requirements: **the buffer address, the file
offset, and the length must all be multiples of the device's logical block size.** Get any of
them wrong and the call fails outright (Windows returns `ERROR_INVALID_PARAMETER`; Linux
returns `EINVAL`) — not slowly, not subtly, just refused.

Declaring the alignment *now*, when it costs one keyword, means doc 12 is a change of I/O
call rather than a rewrite of every allocation site in the engine. It also aligns each page to
a virtual-memory page, which means one page touches one TLB entry instead of straddling two.

The cost: `new Page[1000]` must come back 4096-aligned. C++17 made this automatic —
`operator new` respects over-aligned types. On a pre-C++17 toolchain you would need
`_aligned_malloc` / `posix_memalign`. You are on C++20; it just works. But know *why* it
works, because the day you write a custom allocator for the buffer pool's frame array, this
is the constraint you must preserve.

### 2.4 Why the `static_assert`s are not decoration

`DiskManager::ReadPage` will compute `offset = page_id * PAGE_SIZE` and then read `sizeof(Page)`
bytes into a `Page`. Those two quantities *must* be the same number. If `sizeof(Page)` were
4104 because of a stray member, every read would drift 8 bytes further into the file than the
last, and the corruption would look random and be nearly impossible to trace.

A `static_assert` converts that catastrophe into a compile error with a message. This is the
cheapest bug prevention in the entire series — one line, checked at compile time, zero runtime
cost. Use this pattern for every on-disk structure you define in doc 05.

---

## 3. What is deliberately NOT in Page

The most common design you will see online — including in CMU's BusTub — puts the bookkeeping
inside the page object:

```cpp
class Page {                   // the common design; we are not using it
    char       data_[PAGE_SIZE];
    page_id_t  page_id_;
    int        pin_count_;
    bool       is_dirty_;
};
```

We split it instead:

```cpp
// (this lives in BufferPool.hpp, doc 06 -- shown here only to make the contrast concrete)
struct Frame {
    Page       page;                          // the 4096 bytes that go to disk
    page_id_t  pageId   = INVALID_PAGE_ID;    // bookkeeping -- never written to disk
    int        pinCount = 0;
    bool       dirty    = false;
};
```

Three reasons, in order of how much pain they save you:

1. **`sizeof(Page) == PAGE_SIZE` stays true.** With the combined design it is 4096 + 16ish,
   so you can no longer read or write a `Page` as a unit — every I/O call must remember to
   use `page.data_` and `PAGE_SIZE` rather than the object and its size. One forgetful line
   writes your pin count into the middle of the file. Ask how often that bug has been
   written; the answer is "constantly".
2. **The layering seam stays clean.** `Page` is a disk concept. `pinCount` is a *cache*
   concept — meaningless to `DiskManager`, meaningless on disk, meaningless to the B+Tree.
   Types should not span layers.
3. **You can `memcpy` a `Page` safely.** It is trivially copyable and self-contained. Copying
   a combined page/frame object would duplicate a pin count, which is nonsense.

The cost of splitting: the buffer pool passes `frame.page` around rather than `frame`. That's
it. A rounding error against three classes of prevented bug.

---

## 4. The aliasing rule you are relying on (and will lean on hard in doc 05)

This section is why `std::byte` was chosen, and it is the single most important piece of
language-lawyering in the series. Skipping it means doc 05 looks like arbitrary ritual.

C++ has a rule called **strict aliasing**: the compiler may assume that two pointers of
*different* types never point at the same memory, and optimise accordingly. This is what lets
it keep values in registers instead of reloading them.

```cpp
void f(int* i, float* g) {
    *i = 1;
    *g = 2.0f;        // compiler may assume this did NOT change *i
    // it can legally return the constant 1 here without re-reading memory
}
```

Now consider what doc 05 needs to do: take 4096 raw bytes and read a `uint16_t` count out of
offset 0. That is *exactly* two types viewing one piece of memory. Is it legal?

Yes — because of a specific carve-out. **`char`, `unsigned char`, and `std::byte` may alias
any object type.** You may always view any object as bytes. What you may *not* do is the
reverse: take bytes and pretend they are an `int` by casting the pointer.

```cpp
// UNDEFINED BEHAVIOUR. No int object was ever created at that address.
std::uint16_t count = *reinterpret_cast<std::uint16_t*>(page.data);

// WELL-DEFINED. memcpy creates a new object of the destination type from the byte
// representation. This is the blessed idiom, and it is what doc 05 uses everywhere.
std::uint16_t count;
std::memcpy(&count, page.data, sizeof(count));
```

> **"But `memcpy` is a function call, that must be slower."** It is not. Every mainstream
> compiler recognises a fixed-size `memcpy` and emits a single load instruction. At `-O2` the
> two lines above generate *identical* assembly — one `movzx`. You are getting defined
> behaviour for free. Verify it yourself on godbolt.org; it is a five-minute exercise that
> permanently changes how you write low-level code.

Two related traps, both of which will bite you in doc 05 if you don't know them now:

- **Unaligned access.** `memcpy` handles it correctly regardless of alignment. A pointer cast
  does not: on x86 unaligned loads are merely slow, but on ARM they can fault outright. Since
  your keys sit at arbitrary offsets inside a page, this is not hypothetical.
- **Endianness.** `memcpy` copies your machine's byte order. Fine while the file is only ever
  read on the same architecture — which is our assumption, stated once here so it is a
  decision rather than an oversight. If you ever ship index files between machines, that is
  where explicit little-endian encoding goes.

---

## 5. The complete file

```cpp
#pragma once
// internal/kernal/core/storage/Page.hpp
//
// The unit of transfer between disk and memory. See lesson doc/storage/02-the-page.md.
//
// Page is deliberately opaque: 4096 bytes with a guaranteed size and alignment, and no
// interpretation of its contents. NodePage (doc 05) provides the typed view; BufferPool
// (doc 06) provides the caching; neither is allowed to change what Page *is*.

#include <cstdint>
#include <cstddef>

inline constexpr std::size_t PAGE_SIZE = 4096;

using page_id_t = std::uint32_t;

inline constexpr page_id_t INVALID_PAGE_ID = 0xFFFFFFFFu;

// Page 0 is always the file header (doc 04). Named so that the reservation is explicit
// rather than a magic 0 scattered through the DiskManager.
inline constexpr page_id_t HEADER_PAGE_ID = 0;

struct alignas(PAGE_SIZE) Page {
    std::byte data[PAGE_SIZE];
};

static_assert(sizeof(Page)  == PAGE_SIZE,
              "Page must be exactly PAGE_SIZE bytes -- DiskManager's offset arithmetic "
              "assumes page N starts at N * sizeof(Page)");
static_assert(alignof(Page) == PAGE_SIZE,
              "Page must be PAGE_SIZE-aligned for unbuffered/O_DIRECT I/O in doc 12");
static_assert(std::is_trivially_copyable_v<Page>,
              "Page must be memcpy-able; it is a byte image, not an object with invariants");
```

`std::is_trivially_copyable_v` needs `<type_traits>`. Add it — and note what that third assert
is really saying: *this type has no invariants a copy could break.* That is the formal version
of "it's just bytes", and it is what makes `memcpy`-ing pages to and from disk defensible
rather than reckless.

---

## 6. Deriving fanout — a preview you should do now

You now have enough to answer the question doc 01 §4 raised. If an internal page holds a small
header plus `(key, child)` pairs:

```
  usable   = PAGE_SIZE - header
  perEntry = sizeof(key) + sizeof(page_id_t)

  With a 16-byte header, 8-byte keys, 4-byte page ids:
      usable   = 4096 - 16 = 4080
      perEntry = 8 + 4     = 12
      entries  = 4080 / 12 = 340
```

**Fanout 340.** Compare to your in-memory tree's order 4. Height for 100 million keys:

```
  log_340(100,000,000) = 3.2   ->  4 levels
  log_4  (100,000,000) = 13.3  ->  14 levels
```

At your measured cold-read latency from doc 01 §7, that is the difference between ~80 µs and
~280 µs per lookup. **The entire reason this series exists, in one division.**

Now do the arithmetic that matters: recompute with a 4-byte key. You get 4080/8 = 510 entries,
`log_510(10^8) = 2.9`, still 4 levels — no win. Then try a 32-byte key: 4080/36 = 113,
`log_113(10^8) = 3.9`, still 4. The lesson is that fanout enters as a *logarithm*, so it takes
a large change to move the height at all — but when it does move, it moves by a whole disk
read. Doc 05 §7 returns to this with the real numbers.

---

## Checkpoint

Write `internal/kernal/core/storage/tests/page_test.cpp`:

```cpp
#include "../Page.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>

int main() {
    static_assert(sizeof(Page) == 4096);
    static_assert(alignof(Page) == 4096);
    assert(INVALID_PAGE_ID != HEADER_PAGE_ID);

    // Heap allocation must respect the over-alignment. If this fires, your toolchain is
    // not honouring C++17 aligned new -- doc 12's unbuffered I/O would fail at runtime.
    auto p = std::make_unique<Page>();
    assert(reinterpret_cast<std::uintptr_t>(p.get()) % PAGE_SIZE == 0);

    // Arrays too: element N must also land on a page boundary.
    auto arr = std::make_unique<Page[]>(4);
    for (int i = 0; i < 4; ++i)
        assert(reinterpret_cast<std::uintptr_t>(&arr[i]) % PAGE_SIZE == 0);

    // The blessed round-trip idiom from section 4.
    std::uint16_t in = 0xBEEF, out = 0;
    std::memcpy(p->data, &in, sizeof(in));
    std::memcpy(&out, p->data, sizeof(out));
    assert(in == out);

    std::cout << "page_test OK\n";
}
```

Before doc 03, you should have:

- [ ] `Page.hpp` compiling with `-Wall -Wextra`, no warnings
- [ ] `page_test.cpp` passing, including the alignment assertions
- [ ] An answer to: *why is `INVALID_PAGE_ID` not 0?*
- [ ] An answer to: *why is `memcpy` into a local the correct way to read a `uint16_t` out of
      a page, and a `reinterpret_cast` the wrong way?* — if this one is fuzzy, reread §4. Doc
      05 is built entirely on it.
- [ ] Your fanout arithmetic from §6, done by hand, with your doc 01 latency number applied

Next: [03 — DiskManager I: Raw I/O](03-diskmanager-io.md), where page ids become byte offsets
and bytes actually move.
