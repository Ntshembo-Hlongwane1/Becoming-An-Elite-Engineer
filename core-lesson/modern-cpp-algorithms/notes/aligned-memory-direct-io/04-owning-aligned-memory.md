# Part 4 — Owning Aligned Memory: RAII, Deleters, `span`, Allocators, `pmr`

> Part 3 ended with a table of "allocated with X, release with Y". Humans get that table wrong.
> The C++ answer is to **encode the release function in a type**, so the compiler calls the right
> one exactly once, even when an exception is thrown halfway through. This part builds that up in
> four steps: `unique_ptr` with a deleter → an `AlignedBuffer` class → a standard *Allocator* that
> makes `std::vector` aligned → `std::pmr`.
> Companion reading: `[MEM §4.1, §4.2, §7.3, §17.2, §17.6]`, `[LSM-P2 §10–11]`.

Contents

1. RAII in one paragraph
2. `std::unique_ptr` with a custom deleter (four ways, measured sizes)
3. Designing `AlignedBuffer` — every member justified
4. A real *Allocator*: `AlignedAllocator<T, N>` for standard containers
5. Why `rebind` is not optional here (compiler proof)
6. `std::pmr`: alignment as a run-time parameter (and a trap)
7. Non-owning views: `std::span<char>`
8. Which one to use when
9. Drills

---

## 1. RAII in one paragraph

**R**esource **A**cquisition **I**s **I**nitialisation: tie a resource's lifetime to an object's
lifetime. The constructor acquires the resource and the destructor releases it. C++ guarantees
that destructors of fully constructed automatic objects run when scope is left **for any reason**,
including a `return` in the middle or an exception thrown three calls deep. So the release can't
be forgotten or doubled `[MEM §4.2]`, `[MEM §7.3]`, `[CPPREF-raii]`. Your drill's `FileManager`
already does this for a file descriptor (`close` in the destructor). Now do the same for aligned
memory.

## 2. `std::unique_ptr` with a custom deleter

`std::unique_ptr<T, D>` owns a pointer and calls `D{}(ptr)` in its destructor
`[CPPREF-unique_ptr]`. The default `D` is `std::default_delete<T>`, which calls `delete`. That's
**wrong** for `aligned_alloc` memory (Part 3 §8, case 1), so you supply `D`.

### Four ways to write the deleter **(measured sizes on your VM)**

```cpp
struct FreeDeleter {                                                // (A)
    void operator()(void* p) const noexcept { std::free(p); }
};
std::unique_ptr<char, FreeDeleter>                 a;   // sizeof = 8
std::unique_ptr<char, void(*)(void*)>              b{nullptr, std::free};   // sizeof = 16
auto lam = [](char* p) { std::free(p); };
std::unique_ptr<char, decltype(lam)>               c;   // sizeof = 8
std::unique_ptr<void, std::function<void(void*)>>  d;   // sizeof = 40
```

Line by line:

- **(A) Stateless function object.** `FreeDeleter` has no data members, so it's an *empty
  class*. `unique_ptr` stores it with the empty-base optimisation, which makes it **free**:
  `sizeof` = 8, the same as a raw pointer. `const noexcept` matters: deleters must not throw,
  because they run during stack unwinding.
- **Function pointer** (`void(*)(void*)`) has to be stored, because different `unique_ptr`s could
  hold different functions. That's 8 extra bytes (16 total), and the call is indirect, so it's
  harder to inline.
- **Lambda with no captures** is also an empty class: 8 bytes. `decltype(lam)` names its unique
  type. It's handy locally but awkward in headers.
- **`std::function`** can hold *anything* callable, which costs 40 bytes plus possible heap
  allocation and an indirect call. RocksDB uses this (Part 3 §7a) because its allocator is
  pluggable at run time. That's a deliberate trade-off.

**Systems rule (derived):** use a stateless deleter struct unless you *need* run-time choice.

### A minimal correct aligned buffer with no class of your own

```cpp
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

struct FreeDeleter { void operator()(void* p) const noexcept { std::free(p); } };   // (1)
using AlignedBytes = std::unique_ptr<char, FreeDeleter>;                              // (2)

AlignedBytes MakeAligned(std::size_t align, std::size_t size) {                       // (3)
    // precondition: align is a power of two >= sizeof(void*), size is a multiple of align
    char* p = static_cast<char*>(std::aligned_alloc(align, size));                    // (4)
    if (p == nullptr) throw std::bad_alloc{};                                         // (5)
    return AlignedBytes{p};                                                           // (6)
}

void Demo() {
    AlignedBytes buf = MakeAligned(512, 4096);       // (7)
    std::memset(buf.get(), 'x', 4096);               // (8)
    // ... pwrite(fd, buf.get(), 4096, 0) ...
}                                                    // (9)
```

1. The deleter from (A).
2. A type alias, so the long type is written once.
3. A factory function: the only place that knows *how* the memory was obtained, so the matching
   release (the deleter) lives right next to it.
4. Part 3 §3. The `static_cast` turns `void*` into `char*` so we can index bytes.
5. Convert C-style null-on-failure into the C++ convention (exception). Without this, `buf.get()`
   could be null and the `memset` would crash.
6. Hand ownership to the `unique_ptr` immediately. From this line on, no path can leak.
7. `buf` owns 4096 aligned bytes.
8. `.get()` gives the raw pointer for C APIs. Ownership stays with `buf`.
9. Scope ends, so `FreeDeleter{}(p)` runs, which calls `std::free(p)`. This also happens if anything
   in between threw.

The limitation is that `unique_ptr<char, …>` doesn't know the **size** or **alignment**. The caller
must carry them separately, and that's how off-by-one and "is it aligned?" bugs come back. The
next step fixes that.

## 3. Designing `AlignedBuffer` — every member justified

Goal: one object that *is* "N bytes, aligned to A, zero-filled, released correctly". Here's the
interface Exercise 3 asks you to implement, with the reason for every line. (Bodies are the
exercise.)

```cpp
class AlignedBuffer {
public:
    AlignedBuffer() noexcept = default;                                  // (1)
    static AlignedBuffer Allocate(std::size_t size, std::size_t align);  // (2)

    ~AlignedBuffer();                                                    // (3)
    AlignedBuffer(const AlignedBuffer&) = delete;                        // (4)
    AlignedBuffer& operator=(const AlignedBuffer&) = delete;
    AlignedBuffer(AlignedBuffer&& other) noexcept;                       // (5)
    AlignedBuffer& operator=(AlignedBuffer&& other) noexcept;            // (6)

    char*       data() noexcept;                                         // (7)
    const char* data() const noexcept;
    std::size_t size() const noexcept;                                   // (8)
    std::size_t capacity() const noexcept;
    std::size_t alignment() const noexcept;
    bool        empty() const noexcept;
    std::span<char>       span() noexcept;                               // (9)
    std::span<const char> span() const noexcept;

private:
    char*       data_ = nullptr;                                         // (10)
    std::size_t size_ = 0, capacity_ = 0, align_ = 0;
};
```

1. **An empty state.** A moved-from object needs *some* valid state (see 5), and "empty" is the
   natural one. `noexcept` because it can't fail.
2. **A named factory instead of a constructor.** It makes the call site self-describing
   (`Allocate(4096, 512)`, so you won't mix up the order) and lets you validate before any member
   exists. Invalid alignment → `std::invalid_argument`. Overflow or out of memory →
   `std::bad_alloc`.
3. **Destructor** releases with the function that matches how (2) allocated. This is the whole
   point of the class.
4. **Copy is deleted.** Copying the *pointer* would make two owners, so two frees, a
   **double-free** `[MEM §5.1]`. Copying the *bytes* would be a silent 4 KiB+ allocation, which
   a systems type should never do implicitly. If you want a copy, write `Clone()`.
5. **Move constructor** steals `other`'s pointer and sets `other.data_ = nullptr` (plus zero
   sizes), so exactly one object owns the memory. `noexcept` matters: during growth
   `std::vector` relocates elements with `std::move_if_noexcept`. It moves if the move constructor
   is `noexcept`, and otherwise *copies* when it can, to keep its strong exception guarantee. Copy
   is deleted here, so a non-`noexcept` move would still be used, but the vector would silently
   lose that guarantee. A move that only swaps pointers can't fail, so declare it `noexcept`
   `[CPPREF-move_if_noexcept]`.
6. **Move assignment** must (a) release what `*this` currently owns, (b) steal from `other`, and
   (c) survive `a = std::move(a)` (self-move) without freeing the buffer it is about to keep.
   Exercise 3 tests all three.
7. Raw pointer for syscalls (`pwrite` takes `const void*`).
8. **Two sizes.** `size()` is what the caller asked for. `capacity()` is that rounded **up** to a
   multiple of `align` (Part 1 §8). Two reasons: `aligned_alloc` requires a multiple (Part 3 §3),
   and O_DIRECT lengths must be multiples (Part 5), so a padded tail block always has room.
9. Views for safe iteration and sub-ranges (§7).
10. **Members are the minimal state to release and describe the block.** `data_` is a raw
    pointer, which is fine *inside* the one class whose job is owning it. Alternatively store an
    `AlignedBytes` from §2 and get the move operations for free (`= default`). Try both and
    write down which you prefer in `DECISIONS.md`.

**Zero-filling** (a requirement in Exercise 3): `aligned_alloc` memory "is not zeroed"
`[MAN-aligned_alloc]`. If the buffer is padded to a block and written to disk, the unfilled tail
carries whatever the allocator left there, which might be someone else's freed data (Part 7 §4).
Zeroing costs one `memset` per allocation, which is far cheaper than the disk write it precedes.

## 4. A real *Allocator*: `AlignedAllocator<T, N>`

Your drill wanted to keep writing `std::vector<char>`. You can, if the vector's **allocator**
promises the alignment. A standard container never calls `new` itself. It calls
`std::allocator_traits<Alloc>::allocate(a, n)`, which calls your `a.allocate(n)` `[MEM §17.6]`,
`[CPPREF-allocator_traits]`.

The *Allocator* named requirement `[CPPREF-Allocator]`, reduced to what you must write when
`allocator_traits` fills in the rest:

```cpp
template <class T, std::size_t Align>
class AlignedAllocator {
public:
    using value_type = T;                                                          // (1)
    static constexpr std::size_t alignment =
        Align > alignof(T) ? Align : alignof(T);                                   // (2)

    template <class U> struct rebind { using other = AlignedAllocator<U, Align>; };// (3)

    AlignedAllocator() noexcept = default;                                         // (4)
    template <class U>
    AlignedAllocator(const AlignedAllocator<U, Align>&) noexcept {}                // (5)

    T*   allocate(std::size_t n);                                                  // (6)
    void deallocate(T* p, std::size_t n) noexcept;                                 // (7)

    template <class U>
    bool operator==(const AlignedAllocator<U, Align>&) const noexcept;             // (8)
};
```

1. `value_type` is required. Everything else (`pointer`, `size_type`, …) has defaults in
   `allocator_traits` `[CPPREF-allocator_traits]`.
2. Never align *less* than the type needs. `AlignedAllocator<std::max_align_t, 8>` must still give
   16. (`static_assert` that `Align` is a power of two too.)
3. **`rebind`**, explained in §5. Without it this class doesn't compile with *any* libstdc++
   container.
4. Stateless: every instance is interchangeable. `allocator_traits::is_always_equal` defaults to
   `std::is_empty<Alloc>`, so it's `true` here `[CPPREF-allocator_traits]`.
5. **Converting constructor.** Containers construct `AlignedAllocator<Node>` *from* your
   `AlignedAllocator<int>` (`std::list` does this for its nodes). Required by `[CPPREF-Allocator]`
   ("`A a(b)` … `B(a) == b` and `A(b) == a`"). The book's `CustomAllocator` lacks it (Part 3 §9.4).
6. `allocate(n)` means **n objects, not n bytes**. Your job here:
   - overflow check: `n > SIZE_MAX / sizeof(T)` → throw `std::bad_array_new_length` (what
     `std::allocator` does; the book's version skips it, Part 3 §9.4);
   - `::operator new(n * sizeof(T), std::align_val_t{alignment})` (Part 3 §5a).
7. `deallocate` must use the **matching** aligned `operator delete` (Part 3 §8, case 2). The
   container passes the same `n` back, so you can use the sized overload.
8. Two allocators compare equal if memory from one can be freed by the other. That's always true
   for a stateless allocator. In C++20 `!=` is generated from `==`. (The book omits this too,
   which is why `swap` failed in Part 3 §9.4.)

Usage after Exercise 4:

```cpp
std::vector<char, AlignedAllocator<char, 4096>> buf(4096, 'x');
// buf.data() is 4096-aligned, after every reallocation, for the whole life of the vector.
```

**Subtle point (derived):** `buf.data()` is aligned and `buf.size()` is whatever you made it. The
vector does **not** round lengths. For O_DIRECT you still have to keep `size()` a multiple of the
block (Part 5). An allocator controls *where* memory is, never *how much of it you use*.

## 5. Why `rebind` is not optional here (compiler proof)

`allocator_traits<Alloc>::rebind_alloc<U>` is how a container turns "allocator of `int`" into
"allocator of `ListNode<int>`". Its rule `[CPPREF-allocator_traits]`:

> `Alloc::rebind<T>::other` if present, otherwise `SomeAllocator<T, Args>` if this Alloc is of the
> form `SomeAllocator<U, Args>`, where Args is zero or more **type** arguments.

`AlignedAllocator<T, 4096>` has a **non-type** argument (`4096` is a value, not a type), so the
automatic rule can't apply. **(measured)**: an otherwise-correct allocator without `rebind` fails
to compile with **both** `std::vector` and `std::list` under libstdc++:

```
error: no type named 'type' in 'struct std::__replace_first_arg<AA<int, 64>, int>'
error: no type named 'type' in 'struct std::__allocator_traits_base::__rebind<AA<int, 64>, int>'
```

Add the one-line `rebind` member and both compile. Remember this whenever an allocator template
has a value parameter. The error message never says "add rebind".

## 6. `std::pmr`: alignment as a run-time parameter (and a trap)

`[MEM §17.2]` introduces `std::pmr` ("polymorphic memory resources"). The base class
`std::pmr::memory_resource` has the signature that matters here `[CPPREF-pmr]`:

```cpp
void* allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t));
void  deallocate(void* p, std::size_t bytes, std::size_t alignment = alignof(std::max_align_t));
```

Alignment is a **run-time argument**, and the standard resources honour it **(measured)**:

```
monotonic allocate(4096,512) aligned=1 (after 1-byte alloc, gap=512)
new_delete_resource allocate(4096,4096) aligned=1
```

After a 1-byte allocation, the monotonic resource skipped ahead 512 bytes to give an aligned block.
That's Part 1 §8 align-up inside the library.

**The trap (measured):** `std::pmr::vector<char>` uses `std::pmr::polymorphic_allocator<char>`,
which asks the resource for `alignof(char)` = **1**:

```
pmr::vector<char> data %512=1        ← placed right after a 1-byte allocation
```

In an earlier run without that 1-byte allocation it printed `%512=0`, which was luck again (it
happened to follow a 512-aligned block). **A container requests the alignment of its element
type, no matter what the resource could do.** To use pmr for I/O buffers, call
`resource->allocate(n, 512)` yourself (and wrap that in RAII), or give the element type the
alignment (`struct alignas(512) Sector`).

## 7. Non-owning views: `std::span<char>`

An owner (`AlignedBuffer`) should be passed around **rarely**. Functions that only read or write
the bytes should take a **view**: `std::span<const char>` (read) or `std::span<char>` (write)
`[CPPREF-span]`, `[LSM-P2 §11]`.

```cpp
void WriteAt(std::span<const char> bytes, std::uint64_t offset);   // (1)

AlignedBuffer buf = AlignedBuffer::Allocate(8192, 512);
WriteAt(buf.span(), 0);                         // (2)
WriteAt(buf.span().subspan(512, 1024), 512);    // (3)
```

1. A span is `{pointer, length}`, two words, passed by value. It accepts an `AlignedBuffer`, a
   `std::vector`, a C array, or a sub-range, all with one signature. Compare your drill's
   `Write(std::vector<char>&)`, which accepted only vectors (and a non-const reference, so it
   couldn't take a temporary or a `const` buffer).
2. Whole buffer.
3. A sub-range starting at byte 512 for 1024 bytes. **Alignment does not survive arbitrary
   slicing (derived):** `subspan(512, …)` of a 512-aligned buffer is 512-aligned, but
   `subspan(100, …)` isn't. That's why Exercise 5's `WriteAt` validates the span's address itself
   instead of trusting where it came from.

A span **does not own** anything. If the `AlignedBuffer` dies, the span dangles. Rule: a span may
not outlive the owner it was taken from.

## 8. Which one to use when

| Need | Use | Why |
|---|---|---|
| fixed-size block known at compile time, scoped | `alignas(A)` array/struct | no allocation at all |
| run-time size, simple | `AlignedBytes` (`unique_ptr` + `FreeDeleter`) | 8 bytes, zero overhead |
| run-time size, carries its own size/alignment, zeroed | `AlignedBuffer` (Ex 3) | can't misuse size/alignment |
| a growable container that must stay aligned | `std::vector<T, AlignedAllocator<T, A>>` (Ex 4) | keeps the vector API |
| many aligned blocks with one lifetime | a pmr/arena resource + explicit `allocate(n, A)` | one big allocation, carved (Part 3 §10) |
| every function that just reads/writes bytes | `std::span<const char>` / `std::span<char>` | decoupled from ownership |

## 9. Drills

1. Make `std::unique_ptr<char, FreeDeleter>` own a `posix_memalign` block. Then do the same for
   an `mmap` block. Why does the `mmap` deleter need *state*? (Hint: `munmap` needs the length.)
   What is the new `sizeof`?
2. Write a 10-line program in which an exception is thrown between `aligned_alloc` and `free`.
   Run under ASan, so you see a leak. Fix it with §2 and run again.
3. Put five `AlignedBuffer`s in a `std::vector<AlignedBuffer>` and `push_back` 1000 more. Remove
   `noexcept` from the move constructor. Does it still compile? Check
   `std::is_nothrow_move_constructible_v<AlignedBuffer>` before and after, and explain what
   guarantee the vector lost.
4. Explain why `operator==` returning `true` is correct for `AlignedAllocator` but would be
   **wrong** for an allocator that carries a pointer to an arena.
5. Run the §6 pmr experiment yourself. Then find, in your libstdc++ headers
   (`/usr/include/c++/15/memory_resource`), where `polymorphic_allocator::allocate` passes
   `alignof(_Tp)`.

## My summary

