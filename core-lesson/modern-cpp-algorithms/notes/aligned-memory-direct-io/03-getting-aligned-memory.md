# Part 3 — Getting Aligned Memory: Every Way, Line by Line

> Part 2 showed that the default allocators promise 16. This part covers every tool that promises
> more: what each one is, how it works internally, what it costs, and above all **which function
> must release it**. Mismatched release is the most common mistake here, and the sanitizer catches
> it every time if you let it. Companion reading: `[MEM §14.5, §14.6, §17.2, §17.3, §17.5, §17.6]`.

Contents

1. The decision you are making
2. Way 1 — `alignas` storage (static / stack / member)
3. Way 2 — `std::aligned_alloc`
4. Way 3 — `posix_memalign`
5. Way 4 — `operator new(size, std::align_val_t)` and over-aligned types
6. Way 5 — `mmap`
7. Way 6 — over-allocate and round up (by hand, and with `std::align`)
8. The deallocation table + four sanitizer reports
9. Your memory book, checked against the compiler (errata)
10. What alignment costs
11. Drills

All seven ways were compiled and run together with `-fsanitize=address,undefined` on your VM
**(measured)**:

```
1 static=1 stack=1
2 aligned_alloc=1
3 posix_memalign=1
4 operator new align_val_t=1
5 new Sector[8]=1 sizeof=512
6 mmap=1
7 std::align=1 moved=256 space_left=4351
```

---

## 1. The decision you are making

Every method below answers the same three questions differently:

| Question | Why it matters |
|---|---|
| **Lifetime**: when does the memory go away? | stack memory dies with the function; heap lives until released |
| **Size**: known at compile time, or at run time? | `alignas` arrays need a compile-time size |
| **Release**: *which* function gives it back? | calling the wrong one is undefined behaviour (§8) |

## 2. Way 1 — `alignas` storage

```cpp
alignas(512) static unsigned char s_buf[4096];   // (1)
void f() {
    alignas(512) unsigned char l_buf[4096];      // (2)
    struct Page { alignas(4096) char data[4096]; };  // (3)
}
```

1. A static array whose first byte the linker places on a 512 boundary. It lives for the whole
   program and needs no release.
2. A local array. The compiler realigns the stack frame (Part 2 §2). It dies when `f` returns, so
   **never return a pointer to it** (dangling pointer, `[MEM §1.3.3]`).
3. A member with `alignas` makes the whole struct 4096-aligned with `sizeof` 4096. This is
   PostgreSQL's `PGIOAlignedBlock` pattern `[PG-c.h]`.

Use when the size is a compile-time constant and the lifetime fits the scope. Costs: up to N−1
bytes of padding/stack realignment. Large `alignas` locals use a lot of stack (default 8 MiB on
Linux, `ulimit -s`).

**Pitfall, measured by the compiler:** `-Wmaybe-uninitialized` fired on `l_buf`. Aligned or not,
**storage starts uninitialised**. If you write it to disk without filling it, you write stale
stack bytes (Part 7).

## 3. Way 2 — `std::aligned_alloc` (C++17, from C11)

```cpp
#include <cstdlib>                                         // (1)

constexpr std::size_t kAlign = 512, kSize = 4096;          // (2)
void* a = std::aligned_alloc(kAlign, kSize);               // (3)
if (!a) { std::perror("aligned_alloc"); return 1; }        // (4)
/* ... use a ... */
std::free(a);                                              // (5)
```

1. Declared in `<cstdlib>`, next to `malloc` and `free`. That's a clue about its family.
2. Alignment first, size second. **The order is easy to swap by accident.** Both are
   `std::size_t`, so the compiler won't complain.
3. Returns `void*` (C-style). You'll `static_cast<char*>` it to use it as bytes.
4. On failure it returns a null pointer `[CPPREF-aligned_alloc]`, and glibc sets `errno`
   (`EINVAL` for a bad alignment, `ENOMEM` for out of memory) `[MAN-aligned_alloc]`.
5. "To avoid a memory leak, the returned pointer must be deallocated with std::free or
   std::realloc" `[CPPREF-aligned_alloc]`. **Not `delete`, not `delete[]`.**

The rules, from `[CPPREF-aligned_alloc]`:

- **size** — "An integral multiple of alignment." "Passing a size which is not an integral
  multiple of alignment or an alignment which is not valid or not supported by the implementation
  causes the function to fail and return a null pointer (C11, as published, specified undefined
  behavior in this case, this was corrected by DR460)."
- **alignment** — POSIX-based implementations "inherit" `posix_memalign`'s rules: a power of two
  and a multiple of `sizeof(void*)`.
- **Portability** — "not supported in Microsoft C Runtime library because its implementation of
  std::free is unable to handle aligned allocations of any kind." MSVC has `_aligned_malloc` /
  `_aligned_free` instead.

**(measured)** glibc 2.43 is more lenient than the standard:

```
aligned_alloc(4096,100)=0x6073caf95000 errno=Success      ← size not a multiple, glibc accepted it
aligned_alloc(3000,6000)=(nil) errno=Invalid argument     ← 3000 not a power of two, rejected
```

So code that passes a non-multiple size works on your machine and returns `nullptr` elsewhere.
**Always round the size up** with Part 1 §8's `align_up`, overflow check included.

## 4. Way 3 — `posix_memalign` (POSIX)

```cpp
#include <cstdlib>
#include <cstring>

void* pm = nullptr;                                      // (1)
int rc = posix_memalign(&pm, kAlign, kSize);             // (2)
if (rc != 0) {                                           // (3)
    std::fprintf(stderr, "posix_memalign: %s\n", std::strerror(rc));
    return 1;
}
std::free(pm);                                           // (4)
```

1. An out-parameter. The function writes the address into `pm`.
2. Takes `&pm`, the address of your pointer (a "pointer to pointer", `[MEM §2.3.2]`).
3. It **returns** the error code: "returns zero on success, or one of the error values ... The
   value of errno is not set." `[MAN-posix_memalign]`. Checking `errno` here would be a bug, so
   format `rc` itself.
4. "This address can later be successfully passed to free(3)." `[MAN-posix_memalign]`

Rules `[MAN-posix_memalign]`: alignment "must be a power of two and a multiple of
sizeof(void *)", so `posix_memalign(&p, 2, 64)` fails with `EINVAL` **(measured)**. There is
no size-multiple rule. "The memory is not zeroed."

The man page itself now says that `aligned_alloc` "provides the same functionality, and has a more
ergonomic prototype. Use that instead." You'll still meet `posix_memalign` constantly in older
systems code.

## 5. Way 4 — `operator new(size, std::align_val_t)` and over-aligned types

There are two different things here. You need to keep them apart.

### 5a. Calling the aligned allocation function directly (raw bytes)

```cpp
#include <new>

void* on = ::operator new(kSize, std::align_val_t{kAlign});      // (1)
/* ... */
::operator delete(on, kSize, std::align_val_t{kAlign});          // (2)
```

1. `::operator new` is the **allocation function**. It only gets bytes, and it doesn't construct
   any object. The `std::align_val_t` overload exists since C++17 `[CPPREF-operator_new]`.
   `std::align_val_t` is a **scoped enum** (`enum class align_val_t : std::size_t {}`), chosen so
   that an alignment can't be confused with a size by accident `[P0035]`. You write
   `std::align_val_t{512}`. On failure it **throws `std::bad_alloc`**; it does not return null.
2. Release with the **matching** aligned `operator delete`. You can pass the size too (the sized
   overload), which lets the allocator skip a lookup `[CPPREF-operator_delete]`. Passing the
   alignment is **mandatory**, because the aligned and unaligned paths may use different
   bookkeeping. §8 shows ASan's report when you forget it.

### 5b. Making the *type* over-aligned, so plain `new` does the right thing

```cpp
struct alignas(512) Sector { unsigned char bytes[512]; };   // (1)
Sector* secs = new Sector[8];                                // (2)
delete[] secs;                                               // (3)
```

1. `alignof(Sector) == 512`, `sizeof(Sector) == 512` **(measured)**.
2. Since 512 > `__STDCPP_DEFAULT_NEW_ALIGNMENT__` (16), the compiler calls
   `operator new[](size, std::align_val_t{512})` for you `[CPPREF-operator_new]`, `[P0035]`.
   `secs` is 512-aligned and `secs[1]` is 512 bytes later, which is also aligned.
3. Plain `delete[]`. The compiler picks the matching aligned `operator delete[]` automatically.

This is the most "C++" way to get aligned I/O memory: the alignment is part of the type, so no
call site can forget it. The cost is that sizes come in units of `sizeof(Sector)`.

## 6. Way 5 — `mmap`

```cpp
#include <sys/mman.h>

void* m = mmap(nullptr, kSize, PROT_READ | PROT_WRITE,       // (1)(2)(3)
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);          // (4)(5)
if (m == MAP_FAILED) { std::perror("mmap"); return 1; }      // (6)
munmap(m, kSize);                                            // (7)
```

1. `nullptr` lets the kernel choose the address: "the kernel chooses the (page-aligned) address"
   `[MAN-mmap]`.
2. Length in bytes. The kernel works in whole pages, so it's rounded up to 4096 internally.
3. Readable and writable.
4. `MAP_ANONYMOUS` gives memory not backed by any file, zero-filled. `MAP_PRIVATE` makes it
   private to this process (copy-on-write across `fork`, which matters in Part 5 §10).
5. `fd = -1, offset = 0`, as required for anonymous maps.
6. Failure is `MAP_FAILED`, which is `(void*)-1`, **not** `nullptr`. Comparing to `nullptr` is a
   classic bug.
7. Release with `munmap(addr, length)`, never with `free`.

Use for large buffers (MiBs) where page alignment, zero-fill and direct return to the OS on release
are wanted. The cost is a system call per allocation and at least 4096 bytes of granularity.
glibc itself switches to `mmap` above the threshold for the same reasons (Part 2 §6).

## 7. Way 6 — over-allocate and round up

Every aligned allocator is built on this idea, including glibc's `memalign` internally
**(derived from the overflow check in `[GLIBC-15857-FIX]`, which reserves `alignment` extra bytes)**.

> To get N bytes aligned to A from an allocator that only promises 16: ask for N + A − 1 bytes.
> Somewhere in the first A bytes there *must* be an A-aligned address **(derived: A consecutive
> integers always contain exactly one multiple of A)**. Round up to it with Part 1 §8.

### 7a. How RocksDB does it (production code, line by line)

From `AlignedBuffer::AllocateNewBuffer` `[ROCKS-SRC:aligned_buffer.h]`:

```cpp
size_t new_capacity = Roundup(requested_capacity, alignment_);          // (1)
char* new_buf = new char[new_capacity + alignment_];                    // (2)
char* new_bufstart = reinterpret_cast<char*>(
    (reinterpret_cast<uintptr_t>(new_buf) + (alignment_ - 1)) &         // (3)
    ~static_cast<uintptr_t>(alignment_ - 1));                           // (4)
```

1. Round the usable capacity up to a whole number of alignment units, so lengths are aligned too.
   That's the O_DIRECT length rule (Part 5).
2. Over-allocate by `alignment_` bytes (A − 1 would be enough; A is simpler). `new char[]`
   promises only 16.
3. Convert the address to an integer (Part 1 §4) and add A − 1 …
4. … then clear the low bits. That's align-up (Part 1 §8) applied to an *address*.

The important design point is that RocksDB **keeps both pointers**: `new_buf` (what `new[]`
returned, the only pointer `delete[]` may receive) and `new_bufstart` (the aligned one you do I/O
with). It stores `new_buf` in an owning smart pointer member with a custom deleter, so it's the one
that gets freed:

```cpp
buf_ = std::unique_ptr<void, std::function<void(void*)>>(
    static_cast<void*>(new_buf),
    [](void* p) { delete[] static_cast<char*>(p); });      // releases the ORIGINAL pointer
```

Part 4 §2 covers this deleter pattern in depth.

### 7b. The same thing with `std::align`

`[CPPREF-align]`:

```cpp
void* align(std::size_t alignment, std::size_t size, void*& ptr, std::size_t& space);
```

> "Given a pointer ptr to a buffer of size space, returns a pointer aligned by the specified
> alignment for size number of bytes and decreases space argument by the number of bytes used for
> alignment. ... If the buffer is too small, the function does nothing and returns nullptr."

Look at the signature: `ptr` and `space` are **non-const references**. `std::align`
**modifies your variables**. Used correctly:

```cpp
std::size_t space = kSize + kAlign - 1;                        // (1)
void* raw = std::malloc(space);                                // (2)
void* p = raw;                                                 // (3)
void* aligned = std::align(kAlign, kSize, p, space);           // (4)
// now: aligned == p, aligned is 512-aligned, space shrank by (aligned - raw)
std::free(raw);                                                // (5)
```

1. Enough room for the worst-case adjustment (A − 1) plus the payload.
2. A plain 16-aligned allocation.
3. **A copy**, because `std::align` will overwrite `p`. If you passed `raw` itself you would lose
   the only pointer `free` accepts.
4. On success `p` is moved forward to the aligned address and `space` is reduced. **(measured)**:
   `moved=256 space_left=4351`, so `malloc` returned an address 256 bytes short of a 512
   boundary, and 4607 − 256 = 4351.
5. Free the **original**. Freeing `aligned` is a bad-free (§8, case 3).

### 7c. If you can only keep one pointer: the header trick

`aligned_alloc` returns one pointer and `free` takes that same pointer, so the allocator must be
able to find its own bookkeeping from the aligned pointer. The classic portable technique (a
common interview question, and how you'd build `aligned_alloc` on a platform that lacks it) is to
**store the raw pointer just before the aligned one**:

```
raw (from malloc)                     aligned (returned to user, A-aligned)
│                                     │
▼                                     ▼
┌──────── slack ────────┬─ void* raw ─┬──────────── user bytes (n) ────────────┐
└───────────────────────┴─────────────┴────────────────────────────────────────┘
                         ^ aligned - sizeof(void*): "where did I come from?"
```

You must reserve `sizeof(void*)` extra bytes so the header always fits, and you must pick the
aligned address *after* reserving it. The header slot must itself be suitably aligned to hold a
`void*` (Part 1 §12 says a misaligned `void*` store is UB, so `memcpy` it in and out). **Exercise 2
is to implement exactly this**, with the overflow check from Part 1 §8.

## 8. The deallocation table + four sanitizer reports

| Allocated with | Release with | Never with |
|---|---|---|
| `malloc` / `calloc` / `realloc` | `free` | `delete`, `delete[]` |
| `std::aligned_alloc` / `posix_memalign` | `free` | `delete`, `delete[]`, aligned `operator delete` |
| `new T` | `delete p` | `free`, `delete[]` |
| `new T[n]` | `delete[] p` | `free`, `delete` |
| `::operator new(n)` | `::operator delete(p)` / `(p, n)` | `free` |
| `::operator new(n, align_val_t{A})` | `::operator delete(p, align_val_t{A})` / `(p, n, align_val_t{A})` | unaligned `operator delete`, `free` |
| `mmap(…, len, …)` | `munmap(p, len)` | `free`, `delete` |
| over-allocate + adjust | release the **original** pointer with its own matching function | the adjusted pointer |
| `alignas` local / static | nothing (automatic) | anything |

**(measured)** with `-fsanitize=address,undefined`. Each case is one deliberate violation:

```cpp
// case 1: C allocation, C++ release
char* p = static_cast<char*>(std::aligned_alloc(512, 4096)); delete[] p;
// ==ERROR: AddressSanitizer: alloc-dealloc-mismatch (malloc vs operator delete [])

// case 2: aligned new, unaligned delete
void* p = ::operator new(4096, std::align_val_t{512}); ::operator delete(p);
// ==ERROR: AddressSanitizer: new-delete-type-mismatch

// case 3: freeing the adjusted pointer instead of the original
void* raw = std::malloc(4096 + 511); void* p = raw; std::size_t space = 4096 + 511;
std::align(512, 4096, p, space); std::free(p);
// ==ERROR: AddressSanitizer: attempting free on address which was not malloc()-ed

// case 4: one byte past the end of an aligned block
char* p = static_cast<char*>(std::aligned_alloc(512, 4096)); p[4096] = 1; std::free(p);
// ==ERROR: AddressSanitizer: heap-buffer-overflow
```

Without ASan, none of these is guaranteed to crash. Case 3 is the dangerous one: plain glibc
`free` reads a fake chunk header at `p − 16` (Part 2 §5), which is attacker-relevant heap
corruption (Part 7). **This is why every exercise builds with sanitizers on.**

## 9. Your memory book, checked against the compiler (errata)

Each snippet was compiled with GCC 15.2, `-std=c++20` **(measured)**. These aren't nitpicks:
each one would break or leak in your code.

### 9.1 `[MEM §17.5]` `std::align` example — does not compile

```cpp
void* ptr = malloc(1024);
void* alignedPtr = std::align(alignof(int), sizeof(int), ptr, 1024);
```

```
error: cannot bind non-const lvalue reference of type 'std::size_t&' to a value of type 'int'
```

`space` is `std::size_t&` (§7b). A literal `1024` can't bind to it. Even with a variable, the
example **overwrites `ptr`**, the only pointer `free` accepts. If you then `free(ptr)` and the
pointer moved, that's case 3 above. (It wouldn't move here, because `alignof(int)` = 4 and malloc
gives 16, so the example also demonstrates nothing.) The correct version is §7b.

### 9.2 `[MEM §17.5]` `std::aligned_alloc` example — leaks, and asks for nothing

```cpp
int* alignedMemory = static_cast<int*>(std::aligned_alloc(alignof(int), 100 * sizeof(int)));
```

- No `std::free`, so it leaks (`[CPPREF-aligned_alloc]`: "must be deallocated with std::free").
- No null check.
- `alignof(int)` = 4 is *weaker* than what `malloc` already gives (16, Part 2 §4), so the
  aligned allocator buys nothing. The book's own sentence above it says aligned allocation helps
  "reduce cache misses"; that would need `alignof` ≥ 64 (`[ALGO §22.1.6]`).
- It happens to satisfy the size-multiple rule (400 is a multiple of 4). Don't generalise from
  that (`[LSM-P2 §2]` flagged this too).

### 9.3 `[MEM §14.5]` `std::allocator::construct/destroy` — does not compile in C++20

```cpp
allocator.construct(p + i, i);
allocator.destroy(p + i);
```

```
error: 'class std::allocator<int>' has no member named 'construct'
error: 'class std::allocator<int>' has no member named 'destroy'
```

They were deprecated in C++17 and **removed in C++20** `[CPPREF-allocator]`. It compiles with
`-std=c++17` **(measured)**. The modern spelling is
`std::allocator_traits<A>::construct(a, p, args...)` or `std::construct_at(p, args...)`.

### 9.4 `[MEM §17.2]` / `[MEM §17.6]` `CustomAllocator` — incomplete Allocator

```cpp
template <typename T>
struct CustomAllocator {
    using value_type = T;
    T* allocate(std::size_t n) { return static_cast<T*>(::operator new(n * sizeof(T))); }
    void deallocate(T* p, std::size_t) noexcept { ::operator delete(p); }
};
```

`std::vector<int, CustomAllocator<int>> v; v.push_back(42);` compiles **(measured)**. But
`v.swap(w)` or comparing allocators fails:

```
error: no match for 'operator==' (operand types are 'CustomAllocator<int>' and 'CustomAllocator<int>')
```

The *Allocator* requirements need `a == b` (and a converting constructor from
`CustomAllocator<U>`) `[CPPREF-Allocator]`. Also:

- `n * sizeof(T)` can **overflow** (Part 1 §8, Part 7). `std::allocator` throws
  `std::bad_array_new_length` in that case.
- `::operator new(n * sizeof(T))` ignores `alignof(T)`, so over-aligned `T` is misallocated. That
  is the exact pre-C++17 bug `[P0035]` fixed for `new`, re-introduced by hand.

Part 4 §4 builds the complete version, and Exercise 4 has you implement it.

### 9.5 `[MEM §17.3]` `MemoryPool` — misaligned and leaks

Already measured in `[LSM-P2 §8]`: `pool + offset` with arbitrary sizes returns misaligned
addresses (allocate 1 byte, then an `int` is at offset 1, which is UB per Part 1 §12), and
`new char[size]` has no matching `delete[]`, so it leaks. The fix is align-up on `offset` (Part 1
§8) plus RAII.

## 10. What alignment costs

**(measured)** — two consecutive `aligned_alloc(4096, 4096)` calls:

```
two aligned_alloc(4096,4096): 0x6073caf97000 0x6073caf99000 diff=8192
```

The two 4096-byte blocks are 8192 bytes apart. To find an aligned spot, the allocator
over-reserves (§7), and the slack before each block (plus the chunk header, Part 2 §5) means
**roughly 2× memory for page-aligned page-sized allocations** **(derived from the measurement)**.
That's why engines allocate **one big aligned buffer and carve it** (RocksDB's `AlignedBuffer`
`[ROCKS-SRC:aligned_buffer.h]`, PostgreSQL's aligned shared buffer pool) instead of one aligned
allocation per 4 KiB block.

| Way | Time cost | Space cost | Release |
|---|---|---|---|
| `alignas` static | zero | up to A−1 padding | none |
| `alignas` stack | a few instructions | up to A−1 of stack | none |
| `aligned_alloc` / `posix_memalign` | ~malloc | up to ~A slack + header | `free` |
| aligned `operator new` | ~malloc | same | aligned `operator delete` |
| `mmap` | syscall + page faults | page granularity | `munmap` |
| over-allocate + adjust | ~malloc | A−1 (+ 8 if header) | free the original |

## 11. Drills

1. Write the 7-ways program from the top of this part from memory, without looking. Run it under
   ASan. Then break each release on purpose and read the ASan report.
2. Call `std::aligned_alloc(512, 4096)` 10 000 times (keep the pointers). Compute the average gap
   between consecutive addresses. Repeat with alignment 16, 64, 4096. Plot overhead vs alignment.
3. Fix `[MEM §17.5]`'s `std::align` example so it compiles, is correct, and actually changes the
   pointer. Prove the pointer moved by printing before/after.
4. Explain, in your own words, why freeing an adjusted pointer is worse than leaking it.
5. Why does `posix_memalign` return the error instead of setting `errno`? (Hint: think about
   threads and the age of the API, then check `[MAN-posix_memalign]` *RETURN VALUE*.)

## My summary

