# Part 2 — C++ Memory, From Zero

> An LSM-tree is a machine for moving bytes between three places: C++ objects in RAM, raw byte
> buffers, and files. Every bug in a storage engine lives at one of those boundaries. This part
> teaches the boundary rules. Companion reading: `[MEM ch.2, 4, 14, 15, 17]`, `[ALGO §3.2, §22.1, §23.1]`.

Contents

1. Objects, storage, `sizeof`
2. Alignment — what it is, why it exists, how to get it
3. Padding — the invisible bytes (and an experiment on your algorithms book)
4. Endianness — which byte goes first
5. Looking at an object as bytes: aliasing, `reinterpret_cast`, `memcpy`, `std::bit_cast`
6. Serialization: fixed-width integers, varints, length prefixes
7. The heap: what `new` costs, fragmentation
8. Arenas (a.k.a. bump allocators, monotonic pools) — and an experiment on your memory book
9. Placement new — constructing objects inside raw memory
10. RAII for every resource (memory, fds, mappings)
11. Non-owning views: `std::string_view`, `std::span`, LevelDB's `Slice`
12. Threads from zero: races, critical sections, mutexes
13. Atomics and memory ordering (relaxed / acquire / release), from zero
14. False sharing
15. Summary + drills

---

## 1. Objects, storage, `sizeof`

- An **object** in C++ is a region of storage with a type and a lifetime `[CPPREF-object]`.
- `sizeof(T)` is how many bytes one `T` occupies (including padding, §3).
- The *bytes* of an object are called its **object representation**. Part of that may be
  padding that has no meaning; the meaningful part is the **value representation**
  `[CPPREF-bit_cast]` uses these terms.

Why you care: writing an object to disk means writing *bytes*. Which bytes, in which order, and
whether some of them are garbage, is decided by §2–§4.

## 2. Alignment

From cppreference `[CPPREF-object]`:

> "Every object type has the property called **alignment requirement**, which is a nonnegative
> integer value (of type std::size_t, and always a power of two) representing the number of bytes
> between successive addresses at which objects of this type can be allocated."

In plain words: an `int` with alignment 4 may only live at addresses divisible by 4 (0x1000,
0x1004, …), a `uint64_t` with alignment 8 only at multiples of 8.

- Query it with `alignof(T)`; request stricter alignment with `alignas(N)` `[CPPREF-object]`.
- **"Attempting to create an object in storage that does not meet the alignment requirements of
  the object's type is undefined behavior."** `[CPPREF-object]`
- The largest "normal" alignment is `alignof(std::max_align_t)`; bigger is "extended alignment"
  and "Allocator types are required to handle over-aligned types correctly." `[CPPREF-object]`

**Why does hardware want alignment? (derived from §3 of Part 1)** A cache line is 64 bytes. An
8-byte value at an address divisible by 8 can never straddle two cache lines; at address 0x...3F
it would span two lines (two fetches), and some CPU architectures refuse such accesses entirely.
C++ simply makes the misaligned case UB so the compiler can assume it never happens.

### The power-of-two trick (you'll see it in LevelDB)

Because alignments are powers of two, "address mod align" can be computed with a bit-mask:
`addr & (align - 1)` **(derived)**. Example: align = 8 → mask = `0b111`. Address 0x1003 →
`0x1003 & 7 = 3` → we are 3 bytes past an 8-boundary → we need `8 - 3 = 5` bytes of "slop" to
reach the next aligned address 0x1008.

Hold that thought — it is exactly LevelDB's `Arena::AllocateAligned` (§8).

### Aligned allocation, three ways

| Way | Source |
|---|---|
| `alignas(64) T x;` (static/stack) | `[CPPREF-object]` |
| `std::aligned_alloc(align, size)` / `operator new(size, std::align_val_t{align})` | `[MEM §17.5]` |
| `posix_memalign(&p, align, size)` (POSIX) | `man 3 posix_memalign` |

Caveat to `[MEM §17.5]`'s example: for `std::aligned_alloc`, "The size parameter must be an
integral multiple of alignment" `[CPPREF-aligned_alloc]`. The book's example happens to satisfy
it (`100 * sizeof(int)` with `alignof(int)`) — don't generalise from it; always round the size
up. It belongs to the C allocation family (`malloc`/`free`) listed on the same page, so release
it with `std::free`, never `delete`.

You need aligned buffers for `O_DIRECT` I/O, which "may impose alignment restrictions on the
length and address of user-space buffers and the file offset" `[MAN-open]` (Part 1 §7).

## 3. Padding — the invisible bytes

`[CPPREF-object]`: "In order to satisfy alignment requirements of all non-static members of a
class, padding bits may be inserted after some of its members." Its example:

```cpp
struct X {
    int n;  // size: 4, alignment: 4
    char c; // size: 1, alignment: 1
    // three bytes of padding bits
}; // size: 8, alignment: 4
```

### Experiment on `[ALGO §11.2.2]`

Your algorithms book stores B-tree nodes on disk by writing the raw struct:

```cpp
// [ALGO §11.1.4]
file.write(reinterpret_cast<const char*>(&node), sizeof(node));
```

with this node shape `[ALGO §11.2.2]` (I used `MAX_KEYS = 3`):

```cpp
struct DiskNode {
    bool leaf;
    uint16_t nKeys;
    int keys[MAX_KEYS];
    int values[MAX_KEYS];
    uint64_t childOffsets[MAX_KEYS+1];
};
```

Output on this machine **(measured)**:

```
sizeof(DiskNode)=64 alignof=8
offsetof leaf=0 nKeys=2 keys=4 values=16 childOffsets=32
sum of member sizes=59 -> padding bytes=5
```

Byte map **(derived from the offsets)**:

```
offset: 0    1    2..3    4..15     16..27     28..31    32..63
        leaf PAD  nKeys   keys[3]   values[3]  PAD PAD   childOffsets[4]
```

Five bytes (offset 1 and 28–31) are padding. Consequences when you `write()` the raw struct:

1. **Padding bytes are not reliably initialized.** Whatever was in that memory goes to disk. That
   is (a) an *information leak* — stale heap/stack bytes written to a file (Part 7) — and
   (b) non-determinism: two logically identical nodes can produce different bytes and therefore
   different checksums **(derived)**.
2. **Layout is compiler/ABI-dependent.** Another compiler, another architecture, or a changed
   field order produces a different byte map → old files become unreadable.
3. **Byte order is the CPU's** (next section). The book itself flags "Little-endian vs big-endian
   consistency is important if disk data may move across platforms" `[ALGO §11.1.2]` — but the
   raw-struct write doesn't solve it.

> **Systems-engineer reflex #4:** never write a C++ struct to disk with `write(&s, sizeof s)`.
> Define the **on-disk format in bytes** (a spec), and *encode* each field explicitly (§6).
> That's what every format in Part 6 does.

## 4. Endianness — which byte goes first

`[CPPREF-endian]`: `std::endian` "Indicates the endianness of all scalar types": little-endian
means the least-significant byte is stored at the lowest address; big-endian the reverse.

Experiment **(measured)**:

```cpp
std::uint32_t x = 0x01020304;
unsigned char bytes[4];
std::memcpy(bytes, &x, sizeof x);
// native little? 1
// bytes in memory: 04 03 02 01
```

This machine is little-endian (also confirmed by `lscpu`: "Byte Order: Little Endian").

**The fix: pick an order in the file format and encode byte-by-byte.** LevelDB
`[LDB-SRC:util/coding.h]`:

```cpp
inline void EncodeFixed32(char* dst, uint32_t value) {
  uint8_t* const buffer = reinterpret_cast<uint8_t*>(dst);
  // Recent clang and gcc optimize this to a single mov / str instruction.
  buffer[0] = static_cast<uint8_t>(value);
  buffer[1] = static_cast<uint8_t>(value >> 8);
  buffer[2] = static_cast<uint8_t>(value >> 16);
  buffer[3] = static_cast<uint8_t>(value >> 24);
}

inline uint32_t DecodeFixed32(const char* ptr) {
  const uint8_t* const buffer = reinterpret_cast<const uint8_t*>(ptr);
  // Recent clang and gcc optimize this to a single mov / ldr instruction.
  return (static_cast<uint32_t>(buffer[0])) |
         (static_cast<uint32_t>(buffer[1]) << 8) |
         (static_cast<uint32_t>(buffer[2]) << 16) |
         (static_cast<uint32_t>(buffer[3]) << 24);
}
```

Read it slowly:

- `value >> 8` shifts the number right by one byte; `static_cast<uint8_t>` keeps the low 8 bits.
  So `buffer[0]` = lowest byte, … → **little-endian on every CPU**, regardless of the native order.
- Shifts are defined on *values*, not memory, so this code has **no endianness dependency** and
  **no alignment requirement** on `dst` (it writes single bytes) **(derived)**.
- The comment tells you the compiler turns it into one instruction on little-endian machines —
  portability costs nothing.

### Endianness and *sort order* — a storage-specific trap

Storage engines compare keys as raw bytes (`memcmp`). Experiment **(measured)**:

```
little-endian bytes: memcmp(1,256) = 1  (positive means 1 sorts AFTER 256)
big-endian bytes:    memcmp(1,256) < 0 = 1
strings: memcmp("10","9") < 0 = 1
```

**(derived)** If you want integer keys to sort numerically under a byte-wise comparator, encode
them **big-endian** (most significant byte first). And decimal strings don't sort numerically
("10" < "9"). This is why the *key encoding* is part of the design, not an afterthought.

## 5. Looking at an object as bytes

`[CPPREF-reinterpret]`, "Type aliasing": you may read an object through a glvalue of type
`char`, `unsigned char` or `std::byte` — "this permits examination of the object representation
of any object as an array of bytes." Reading through an *unrelated* type is UB: "If a program
attempts to read or modify the stored value of an object through a glvalue through which it is
not type-accessible, the behavior is undefined." The reason: "This rule enables type-based alias
analysis, in which a compiler assumes that the value read through a glvalue of one type is not
modified by a write to a glvalue of a different type."

What that means in practice:

| Code | OK? |
|---|---|
| `reinterpret_cast<const char*>(&node)` then read bytes | ✅ char may alias anything |
| `auto* p = reinterpret_cast<const uint32_t*>(buf + 5); *p` | ❌ UB: no `uint32_t` object lives there, and `buf + 5` may be misaligned (§2) |
| `uint32_t v; std::memcpy(&v, buf + 5, 4);` | ✅ copies bytes into a real `uint32_t` |
| `auto v = std::bit_cast<uint32_t>(arr4bytes);` | ✅ C++20; "Obtain a value of type To by reinterpreting the object representation of From" `[CPPREF-bit_cast]` |
| `DecodeFixed32(buf + 5)` (shift-and-or, §4) | ✅ and endian-independent |

> **Rule:** bytes come off the disk into a `char`/`std::byte` buffer. They become integers only
> through `memcpy`, `bit_cast` or explicit decoding — never by casting the pointer.

## 6. Serialization: fixed-width, varints, length prefixes

**Serialization** = turning in-memory values into a defined byte sequence (and back).

### Fixed-width

`EncodeFixed32/64` above. Use when you need random access ("the restart array is a list of
uint32"; "the footer is exactly 48 bytes" — Part 6) or when the value is usually large.

### Varints (variable-length integers)

From the Protocol Buffers spec `[PB-ENC]`, which LevelDB's table format explicitly points to
`[LDB-TABLE]`:

> "Each byte in the varint has a continuation bit that indicates if the byte that follows it is
> part of the varint. This is the most significant bit (MSB) of the byte … The lower 7 bits are a
> payload … These 7-bit payloads are in little-endian order."

Worked example from the spec: 150 → `96 01`:

```
10010110 00000001        // Original inputs.
 0010110  0000001        // Drop continuation bits.
 0000001  0010110        // Convert to big-endian.
   00000010010110        // Concatenate.
 128 + 16 + 4 + 2 = 150
```

My encoder reproduces it **(measured)**:

```
         1 -> 01  (1 bytes)
       127 -> 7f  (1 bytes)
       128 -> 80 01  (2 bytes)
       150 -> 96 01  (2 bytes)
       300 -> ac 02  (2 bytes)
     16384 -> 80 80 01  (3 bytes)
4294967295 -> ff ff ff ff 0f  (5 bytes)
```

So a 32-bit varint takes 1–5 bytes. Small lengths (most keys are < 128 bytes) cost **one** byte
instead of four. LevelDB uses varints for every key/value length inside blocks `[LDB-SRC:table/block_builder.cc]`.

**Decoding safely** — LevelDB's decoder `[LDB-SRC:util/coding.cc]`:

```cpp
const char* GetVarint32PtrFallback(const char* p, const char* limit, uint32_t* value) {
  uint32_t result = 0;
  for (uint32_t shift = 0; shift <= 28 && p < limit; shift += 7) {
    uint32_t byte = *(reinterpret_cast<const uint8_t*>(p));
    p++;
    if (byte & 128) {
      // More bytes are present
      result |= ((byte & 127) << shift);
    } else {
      result |= (byte << shift);
      *value = result;
      return reinterpret_cast<const char*>(p);
    }
  }
  return nullptr;
}
```

Two defensive limits, both essential when the bytes come from a file that might be corrupt or
malicious (Part 7): `p < limit` (never read past the buffer) and `shift <= 28` (at most 5 bytes —
no runaway loop). Returning `nullptr` signals "corrupt".

### Length prefixes

To store a variable-length string you write its **length first**, then the bytes. LevelDB's
memtable entry is exactly that `[LDB-SRC:db/memtable.cc]`:

```
key_size   : varint32
key bytes  : char[key_size]
value_size : varint32
value bytes: char[value_size]
```

A length prefix is a *claim made by the file* about how many bytes follow. A parser must check
the claim against the bytes actually available — trusting it is how buffer over-reads happen.

## 7. The heap: what `new` costs

`new`/`malloc` must find a free chunk of the right size, record bookkeeping, and later take it
back on `delete`. OSTEP ch. 17 `[OSTEP-17]` describes the core problem:

- **External fragmentation**: "the free space gets chopped into little pieces of different sizes
  … subsequent requests may fail because there is no single contiguous space that can satisfy
  the request, even though the total amount of free space exceeds the size of the request."
- **Internal fragmentation**: "if an allocator hands out chunks of memory bigger than that
  requested, any unasked for (and thus unused) space in such a chunk".
- **Segregated lists / slab allocators**: "if a particular application has one (or a few) popular-
  sized request that it makes, keep a separate list just to manage objects of that size".

A memtable inserts millions of small, variable-sized entries and then frees *all of them at
once* when it is flushed. Calling `new` per entry pays per-allocation bookkeeping you never need
— you never free entries individually **(derived)**. Enter the arena.

## 8. Arenas (bump allocators)

### The idea

Grab a big block once. Hand out memory by **bumping a pointer** forward. Never free individual
objects; free **all blocks at once** when the arena dies. Allocation is an add and a compare.

Both your books describe this. `[MEM §17.3]` ("Memory pools are pre-allocated blocks of memory
from which smaller objects are dynamically allocated") and `[MEM §18.2]` ("memory pools (also
known as memory arenas) provide a more deterministic approach"). The C++ standard library has
one: `std::pmr::monotonic_buffer_resource` "releases the allocated memory only when the
resource is destroyed. It is intended for very fast memory allocations in situations where
memory is used to build up a few objects and then is released all at once." `[CPPREF-mbr]`
(Also: "monotonic_buffer_resource is not thread-safe.")

### Experiment on `[MEM §17.3]`'s `MemoryPool`

The book's pool:

```cpp
class MemoryPool {
public:
    MemoryPool(size_t size) : poolSize(size), pool(new char[size]), offset(0) {}
    void* allocate(size_t size) {
        if (offset + size > poolSize) throw std::bad_alloc();
        void* ptr = pool + offset;
        offset += size;
        return ptr;
    }
    void deallocate(void* ptr) { /* No-op */ }
private:
    size_t poolSize; char* pool; size_t offset;
};
```

I allocated 1 byte, then a `uint64_t`, compiled with `-fsanitize=address,undefined` **(measured)**:

```
address % alignof(uint64_t) = 1
pool_book.cpp:18:8: runtime error: store to misaligned address 0x7af3be7e0081 for type
'long unsigned int', which requires 8 byte alignment
...
==494456==ERROR: LeakSanitizer: detected memory leaks
```

Three bugs, each a lesson:

1. **No alignment** → UB the moment you place anything but `char` after an odd-sized allocation
   (§2). The book's usage example `static_cast<int*>(pool.allocate(sizeof(int)))` only works
   because it's the first allocation.
2. **No destructor** → the `new char[size]` block is never freed (leak). An arena *owns* its
   blocks; RAII says the destructor must release them (§10).
3. **Copyable** → the compiler-generated copy constructor would copy the raw `pool` pointer; once
   you add a destructor, two copies would both `delete[]` it (double free) **(derived)**. Delete
   the copy operations.

### The production version: LevelDB's `Arena`

`[LDB-SRC:util/arena.h]` and `[LDB-SRC:util/arena.cc]` (abridged, comments mine where marked):

```cpp
class Arena {
 public:
  Arena();
  Arena(const Arena&) = delete;             // fixes bug 3
  Arena& operator=(const Arena&) = delete;
  ~Arena();                                 // fixes bug 2: deletes every block
  char* Allocate(size_t bytes);
  char* AllocateAligned(size_t bytes);      // fixes bug 1
  size_t MemoryUsage() const { return memory_usage_.load(std::memory_order_relaxed); }
 private:
  char* AllocateFallback(size_t bytes);
  char* AllocateNewBlock(size_t block_bytes);
  char* alloc_ptr_;
  size_t alloc_bytes_remaining_;
  std::vector<char*> blocks_;               // every block ever allocated
  std::atomic<size_t> memory_usage_;
};

inline char* Arena::Allocate(size_t bytes) {
  assert(bytes > 0);
  if (bytes <= alloc_bytes_remaining_) {    // fast path: bump
    char* result = alloc_ptr_;
    alloc_ptr_ += bytes;
    alloc_bytes_remaining_ -= bytes;
    return result;
  }
  return AllocateFallback(bytes);
}

static const int kBlockSize = 4096;

char* Arena::AllocateFallback(size_t bytes) {
  if (bytes > kBlockSize / 4) {
    // Object is more than a quarter of our block size.  Allocate it separately
    // to avoid wasting too much space in leftover bytes.
    char* result = AllocateNewBlock(bytes);
    return result;
  }
  // We waste the remaining space in the current block.
  alloc_ptr_ = AllocateNewBlock(kBlockSize);
  alloc_bytes_remaining_ = kBlockSize;
  char* result = alloc_ptr_;
  alloc_ptr_ += bytes;
  alloc_bytes_remaining_ -= bytes;
  return result;
}

char* Arena::AllocateAligned(size_t bytes) {
  const int align = (sizeof(void*) > 8) ? sizeof(void*) : 8;
  static_assert((align & (align - 1)) == 0, "Pointer size should be a power of 2");
  size_t current_mod = reinterpret_cast<uintptr_t>(alloc_ptr_) & (align - 1);
  size_t slop = (current_mod == 0 ? 0 : align - current_mod);
  size_t needed = bytes + slop;
  char* result;
  if (needed <= alloc_bytes_remaining_) {
    result = alloc_ptr_ + slop;
    alloc_ptr_ += needed;
    alloc_bytes_remaining_ -= needed;
  } else {
    // AllocateFallback always returned aligned memory
    result = AllocateFallback(bytes);
  }
  assert((reinterpret_cast<uintptr_t>(result) & (align - 1)) == 0);
  return result;
}
```

Walk-through **(derived)**:

- `align` = 8 on 64-bit. `static_assert` proves at compile time it's a power of two, so the mask
  trick from §2 is valid.
- `current_mod` = how far past an 8-boundary we are; `slop` = bytes to skip to reach the next
  boundary. Exactly the 0x1003 → skip 5 → 0x1008 example.
- Why is "AllocateFallback always … aligned"? A fresh block comes from `new char[]`, and
  since C++17 global `operator new` returns storage "aligned to `__STDCPP_DEFAULT_NEW_ALIGNMENT__`"
  `[CPPREF-operator_new]` (16 on x86-64 GCC), which is ≥ 8. (The comment asserts it; the
  `assert` checks it.)
- **The 1/4 rule**: a request bigger than 1 KiB gets its own block; otherwise the arena may
  abandon the rest of the current block. So waste per block is bounded by < 1 KiB out of 4 KiB —
  a deliberate trade of internal fragmentation for speed `[OSTEP-17]` vocabulary.
- `memory_usage_` is the only atomic: other threads read `MemoryUsage()` (to decide "is the
  memtable full?") while the writer allocates. Even the LevelDB authors left a TODO asking
  whether mixing atomic and non-atomic members "is OK" — a nice reminder that concurrency
  reasoning is hard.

Why an arena is *perfect* for a memtable **(derived)**: entries are never individually deleted
(an LSM "delete" is an *insert* of a tombstone — Part 5), and the whole memtable dies at once
after flush. Arena lifetime = memtable lifetime.

## 9. Placement new

You have raw arena bytes; you need a *constructed object* in them. `[CPPREF-new]`:

> "the standard allocation function `void* operator new(std::size_t, void*)`, which simply returns
> its second argument unchanged … is used to construct objects in allocated storage"

```cpp
alignas(T) unsigned char buf[sizeof(T)];
T* tptr = new(buf) T; // Construct a "T" object, placing it directly into your pre-allocated storage
```

LevelDB builds skip-list nodes this way, and with a twist — a node's size depends on its random
height `[LDB-SRC:db/skiplist.h]`:

```cpp
template <typename Key, class Comparator>
typename SkipList<Key, Comparator>::Node* SkipList<Key, Comparator>::NewNode(
    const Key& key, int height) {
  char* const node_memory = arena_->AllocateAligned(
      sizeof(Node) + sizeof(std::atomic<Node*>) * (height - 1));
  return new (node_memory) Node(key);
}
```

and `Node` ends with `std::atomic<Node*> next_[1];` — "Array of length equal to the node
height". The allocation is sized for `height` pointers even though the type declares 1: a
"trailing array" trick. Two rules make it work **(derived)**: the memory is aligned
(`AllocateAligned`), and nodes are never destroyed individually (the arena frees raw bytes; no
destructors run — fine because `Node` holds nothing that needs destruction).

> Placement new rule of thumb: you are now responsible for (1) alignment, (2) calling the
> destructor if the type has a non-trivial one, (3) never using the object after its storage is
> freed.

## 10. RAII for every resource

`[CPPREF-raii]`: RAII "binds the life cycle of a resource that must be acquired before use
(allocated heap memory, thread of execution, open socket, open file, locked mutex, disk space …)
to the lifetime of an object … the constructor acquires the resource … the destructor releases
the resource and never throws exceptions". The memory book covers it in `[MEM §4.2]` and
`[MEM §11.1]`.

In a storage engine the resources are: heap blocks (arena), **file descriptors**, `mmap`
mappings, locks, and *files on disk* (a half-written temp SSTable that must be deleted on
failure). Sketch of the fd wrapper you'll need in Exercise 2:

```cpp
class Fd {
public:
    explicit Fd(int fd) noexcept : fd_(fd) {}
    ~Fd() { if (fd_ >= 0) ::close(fd_); }
    Fd(const Fd&) = delete;               // one owner
    Fd& operator=(const Fd&) = delete;
    Fd(Fd&& o) noexcept : fd_(std::exchange(o.fd_, -1)) {}
    Fd& operator=(Fd&& o) noexcept { if (this != &o) { reset(); fd_ = std::exchange(o.fd_, -1); } return *this; }
    int get() const noexcept { return fd_; }
    void reset() noexcept { if (fd_ >= 0) ::close(fd_); fd_ = -1; }
private:
    int fd_ = -1;
};
```

This is the "make it impossible by construction" fix for your `master-file-manager` fd leak.

## 11. Non-owning views

`std::string_view` and `std::span` `[ALGO §3.2.3–3.2.4]` are a pointer + length that **do not own**
memory. LevelDB's equivalent is `Slice` (`data()`, `size()`). Every `Slice` returned by the
memtable points *into the arena*; every `Slice` returned by a block reader points *into a block
buffer* `[LDB-SRC:table/format.cc]`.

**(derived)** A view is valid only while the thing it points into is alive. If the memtable is
destroyed after flush while an iterator still holds a `Slice` into it, that is a use-after-free.
This is why LSM engines **reference-count** memtables and SSTables (Part 5 §10) — lifetime
management is a correctness requirement, not an optimisation.

## 12. Threads from zero

OSTEP ch. 26 `[OSTEP-26]`, in their words:

- A **race condition** (data race): "the results depend on the timing of the code's execution".
  Their classic example: two threads each doing `counter = counter + 1` lose updates, because one
  C statement is several machine instructions (load, add, store) and a thread can be interrupted
  between them.
- A **critical section** "is a piece of code that accesses a shared variable … and must not be
  concurrently executed by more than one thread."
- **Mutual exclusion** guarantees only one thread is inside the critical section.
- **Atomicity**: "'all or nothing'; it should either appear as if all of the actions you wish to
  group together occurred, or that none of them occurred, with no in-between state visible."

In C++, a `std::mutex` provides mutual exclusion (`[MEM §6.3]`, `[ALGO §23.1.2]`). LevelDB's skip
list documents its policy `[LDB-SRC:db/skiplist.h]`:

> "Writes require external synchronization, most likely a mutex. Reads require a guarantee that
> the SkipList will not be destroyed while the read is in progress. Apart from that, reads
> progress without any internal locking or synchronization."

How can readers run *with no lock* while a writer modifies the list? That needs atomics and
memory ordering.

## 13. Atomics and memory ordering, from zero

### Why plain variables are not enough

Two separate problems **(derived from `[OSTEP-26]` + `[CPPREF-memorder]`)**:

1. **Tearing / data race**: if one thread writes a plain variable while another reads it, C++
   calls that a data race → undefined behaviour. `std::atomic` fixes this: "If one thread writes to
   an atomic object while another thread reads from it, the behavior is well-defined"
   `[CPPREF-atomic]`.
2. **Reordering**: compilers and CPUs reorder memory operations for speed. A writer that does
   "(1) fill in a new node, (2) link it into the list" might have (2) become visible to another
   core *before* (1). A reader would then follow a pointer into a half-initialised node.

### The orders you need (all quotes `[CPPREF-memorder]`)

- `memory_order_relaxed`: "not synchronization operations; they do not impose an order among
  concurrent memory accesses. They only guarantee atomicity and modification order consistency."
  → good for counters/statistics (`Arena::MemoryUsage()` uses it).
- `memory_order_release` (on a store): "no reads or writes in the current thread can be reordered
  after this store. All writes in the current thread are visible in other threads that acquire
  the same atomic variable".
- `memory_order_acquire` (on a load): "no reads or writes in the current thread can be reordered
  before this load. All writes in other threads that release the same atomic variable are visible
  in the current thread".
- Together, **release–acquire**: "If an atomic store in thread A is tagged memory_order_release, an
  atomic load in thread B from the same variable is tagged memory_order_acquire, and the load in
  thread B reads a value written by the store in thread A, then the store in thread A
  synchronizes-with the load in thread B … once the atomic load is completed, thread B is
  guaranteed to see everything thread A wrote to memory."
- `memory_order_seq_cst` (the default): acquire/release plus "a single total order exists in which
  all threads observe all modifications in the same order".

The memory book's producer/consumer example `[MEM §15.2]` is the canonical pattern:

```cpp
std::atomic<bool> ready(false);
int data = 0;
void producer() { data = 42; ready.store(true, std::memory_order_release); }
void consumer() { while (!ready.load(std::memory_order_acquire)); std::cout << data; } // prints 42
```

Hardware note: "On strongly-ordered systems — x86 … release-acquire ordering is automatic for
the majority of operations. No additional CPU instructions are issued … only certain compiler
optimizations are affected … On weakly-ordered systems (ARM …), special CPU load or memory fence
instructions are used." `[CPPREF-memorder]` — so a missing `acquire` may *appear* to work on your
x86 machine and break on ARM (relevant to your embedded goals).

### The skip list's "publish" pattern

`[LDB-SRC:db/skiplist.h]`:

```cpp
Node* Next(int n) {
  // Use an 'acquire load' so that we observe a fully initialized
  // version of the returned Node.
  return next_[n].load(std::memory_order_acquire);
}
void SetNext(int n, Node* x) {
  // Use a 'release store' so that anybody who reads through this
  // pointer observes a fully initialized version of the inserted node.
  next_[n].store(x, std::memory_order_release);
}
```

and inside `Insert`:

```cpp
x = NewNode(key, height);
for (int i = 0; i < height; i++) {
  // NoBarrier_SetNext() suffices since we will add a barrier when
  // we publish a pointer to "x" in prev[i].
  x->NoBarrier_SetNext(i, prev[i]->NoBarrier_Next(i));
  prev[i]->SetNext(i, x);
}
```

Reading it **(derived)**: the new node's own fields (key, its forward pointers) are written with
relaxed/no-barrier stores *because nobody can see the node yet*. The single `release` store into
the predecessor (`prev[i]->SetNext`) is the **publication**. Any reader that `acquire`-loads that
pointer is guaranteed to see the fully initialised node. Combined with invariants from the same
file — "(1) Allocated nodes are never deleted until the SkipList is destroyed" and "(2) The
contents of a Node except for the next/prev pointers are immutable after the Node has been
linked" — readers never need a lock.

This is the deepest C++ idea in the LSM-tree. If it feels slippery, re-read §13 twice and do
drill 5.

## 14. False sharing

Two threads updating *different* variables that sit in the *same* 64-byte cache line fight over
that line. Your algorithms book: "Avoid false sharing: Ensure frequently modified fields are not
shared across threads on the same cache line." `[ALGO §22.1.6]`. C++17 gives the constant
`std::hardware_destructive_interference_size`, "Minimum offset between two objects to avoid
false sharing" `[CPPREF-hdis]`:

```cpp
struct keep_apart {
    alignas(std::hardware_destructive_interference_size) std::atomic<int> cat;
    alignas(std::hardware_destructive_interference_size) std::atomic<int> dog;
};
```

In an LSM engine: per-thread statistics counters, or the "last sequence number" vs. "memtable
size" counters, are candidates.

## 15. Summary

| Concept | One-line rule | Where it shows up in the LSM |
|---|---|---|
| Alignment | objects must sit at multiples of `alignof(T)`; misaligned = UB | arena, skip-list nodes, O_DIRECT buffers |
| Padding | structs contain garbage bytes | never `write(&struct)`; encode fields |
| Endianness | fix a byte order in the format; big-endian keys sort numerically | every integer in every file; key encoding |
| Aliasing | bytes → integers via memcpy/bit_cast/decoding only | every parser |
| Varint | 7 bits/byte + continuation bit; bound the loop | lengths in blocks, memtable entries |
| Arena | bump pointer, free all at once, align explicitly | memtable |
| Placement new | construct in raw memory; you own alignment + destruction | skip-list nodes |
| RAII | every resource owned by an object | fds, temp files, arenas, iterators |
| Views | non-owning; valid only while the owner lives | Slices into arena / blocks → ref counting |
| Release/acquire | release-store publishes; acquire-load observes | lock-free skip-list readers |

## Drills

1. Print `sizeof`/`alignof`/`offsetof` for a struct you'd naively write to disk in your
   object-storage project. Draw its byte map like §3.
2. Write `EncodeFixed64/DecodeFixed64` and `PutVarint64/GetVarint64(p, limit)`. Fuzz the decoder
   with random byte strings under `-fsanitize=address,undefined` for 60 seconds.
3. Fix the `[MEM §17.3]` pool: add alignment, destructor, delete copies. Re-run my experiment under
   UBSan/ASan until it is clean.
4. Implement `Arena` from memory, then diff against LevelDB's. Explain every difference.
5. Write the `[MEM §15.2]` producer/consumer with `relaxed` instead of `release`/`acquire`. Explain
   (in words, citing `[CPPREF-memorder]`) why it is now wrong even if it prints 42 on x86.

## My summary

_(write 5 lines here)_
