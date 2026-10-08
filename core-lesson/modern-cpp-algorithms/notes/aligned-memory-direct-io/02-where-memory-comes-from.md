# Part 2 — Where Memory Comes From (and What Alignment It Promises)

> You never choose the address of your bytes. Some *allocator* does: the compiler for stack and
> static storage, `malloc` for the heap, the kernel for `mmap`. Each one makes an **alignment
> promise**, and each promise is a contract you can look up. Your bug was a mismatch between
> contracts: `std::vector` promised 16, `O_DIRECT` needed 512.
> Companion reading: `[MEM §1.2, §2.1, §14.6]`.

Contents

1. Three kinds of storage
2. Static and stack storage: the compiler decides
3. The measurement program (run it yourself)
4. The heap: what `malloc` promises, and why 16
5. Inside glibc: the chunk header that shifts your pointer
6. Big allocations: the mmap threshold (a nasty surprise)
7. `operator new` and `std::vector`: the same promise
8. "It worked on my machine": alignment by luck
9. Summary table
10. Drills

---

## 1. Three kinds of storage

`[MEM §1.2]` lists three memory types: **static** (globals, `static` locals, which live for the
whole program), **stack** (locals, released automatically in LIFO order when the function
returns), and **heap** ("dynamic memory allocation at runtime ... requires explicit management").
For alignment, the question is the same for each: *who picks the address, and what do they
promise?*

## 2. Static and stack storage: the compiler decides

For a variable whose storage the compiler lays out (globals, statics, locals), the compiler
**honours `alignof` and `alignas` itself**:

- Global / `static`: the compiler emits an alignment directive in the object file and the linker
  places the variable on that boundary.
- Stack: the x86-64 System V ABI requires the stack pointer to be **16-byte aligned** at every
  function call `[SYSV-ABI §3.2.2]`. A local with `alignas(512)` makes the compiler emit extra
  instructions to round the stack pointer down to a multiple of 512 for that frame
  **(derived — try Drill 4 to see the `and rsp, -512` instruction)**.

**(measured)**:

```
static alignas(4096) %4096=0, stack alignas(512) %512=0
plain stack int %16=4
```

The last line is worth a second look. A plain `int` local was at an address ≡ 4 (mod 16). That's
fine, because `int` only needs 4. The compiler guarantees **exactly what the type asks for and
nothing more**. Remember this pattern: **a guarantee is a minimum, and any extra alignment you see
is luck.**

## 3. The measurement program (run it yourself)

Every **(measured)** number in this part comes from these programs, compiled with
`g++ -std=c++20 -O0` on your VM. They're short, so here is each one line by line.

```cpp
#include <cstddef>      // std::max_align_t, std::size_t
#include <cstdint>      // std::uintptr_t
#include <cstdio>       // std::printf
#include <cstdlib>      // std::malloc, std::free
#include <new>          // __STDCPP_DEFAULT_NEW_ALIGNMENT__, hardware_destructive_interference_size
#include <vector>
#include <malloc.h>     // malloc_usable_size (glibc extension)

// Largest power of two (up to 64 KiB) that divides p's address = its "natural alignment".
static unsigned natural(void* p) {
    auto x = reinterpret_cast<std::uintptr_t>(p);   // Part 1 §4: look at the address as a number
    unsigned a = 1;
    while (a < 65536 && x % (a * 2) == 0) a *= 2;   // keep doubling while still divisible
    return a;
}

int main() {
    std::printf("alignof(max_align_t)=%zu\n", alignof(std::max_align_t));
    std::printf("__STDCPP_DEFAULT_NEW_ALIGNMENT__=%zu\n", (size_t)__STDCPP_DEFAULT_NEW_ALIGNMENT__);
    std::printf("hardware_destructive_interference_size=%zu\n",
                std::hardware_destructive_interference_size);

    unsigned hist[17] = {};                 // hist[b] counts addresses whose natural alignment is 2^b
    std::vector<void*> keep;                // keep them alive so malloc can't hand the same block back
    for (int i = 0; i < 1000; i++) {
        void* p = std::malloc(4096);
        keep.push_back(p);
        unsigned a = natural(p);
        int b = 0; while ((1u << b) < a) b++;   // b = log2(a)
        hist[b]++;
    }
    for (int b = 0; b < 17; b++) if (hist[b]) std::printf("  %6u : %u\n", 1u << b, hist[b]);
    for (void* p : keep) std::free(p);      // every malloc gets exactly one free

    void* p = std::malloc(4096);
    std::printf("malloc_usable_size(malloc(4096))=%zu\n", malloc_usable_size(p));
    std::free(p);
}
```

The only non-obvious line is `keep.push_back(p)`. If each block were freed immediately, `malloc`
would hand the **same** block back 1000 times and the histogram would contain a single address.

Output **(measured)**:

```
alignof(max_align_t)=16
__STDCPP_DEFAULT_NEW_ALIGNMENT__=16
hardware_destructive_interference_size=64
malloc(4096) x1000, largest power-of-2 dividing address:
      16 : 501
      32 : 250
      64 : 125
     128 : 62
     256 : 31
     512 : 16
    1024 : 7
    2048 : 3
    4096 : 3
    8192 : 1
   32768 : 1
malloc_usable_size(malloc(4096))=4104
```

## 4. The heap: what `malloc` promises, and why 16

The contract `[MAN-malloc]`:

> "The malloc(), calloc(), realloc(), and reallocarray() functions return a pointer to the
> allocated memory, which is suitably aligned for any type that fits into the requested size or
> less."

"Any type" means any type with *fundamental* alignment, and the strictest of those is
`std::max_align_t`, which is 16 on your VM **(measured)**. That's why it's 16 and not 8: `long
double` has alignment 16 on x86-64 **(measured, Part 1 §10)**, and `malloc` must be able to hold
one.

**Nothing in that contract mentions 512 or 4096.** A 512-byte requirement is *extended* alignment
(Part 1 §10), and plain `malloc` doesn't serve it.

Now read the histogram as a probability distribution. Every count is about half the one above it:
501, 250, 125, 62, 31, 16, … An address that's guaranteed 16-aligned but otherwise arbitrary has
bit 4 always 0, and each higher bit is 0 or 1 with roughly 50/50 odds **(derived)**. So:

- P(at least 512-aligned) ≈ (16+7+3+3+1+1)/1000 = **3.1 %** **(measured)**
- P(at least 4096-aligned) ≈ 5/1000 = **0.5 %** **(measured)**

So if you had run your drill a few dozen times, a write might have succeeded once, and you would
have had a heisenbug. That is the most dangerous kind of alignment bug.

## 5. Inside glibc: the chunk header that shifts your pointer

Why isn't `malloc(4096)` just page-aligned? glibc stores **bookkeeping right before the pointer
it returns**. From the source comment `[GLIBC-malloc.c]`:

```
    chunk-> +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            |             Size of previous chunk, if unallocated (P clear)  |
            +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            |             Size of chunk, in bytes                     |A|M|P|
      mem-> +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            |             User data starts here...                          .
```

> "'chunk' is the front of the chunk for the purpose of most of the malloc code, but 'mem' is the
> pointer that is returned to the user."

So glibc carves memory into **chunks**. Each starts with two 8-byte size fields, and you get the
address **after** them. On a 64-bit system those two fields take 16 bytes **(derived: 2 ×
`sizeof(size_t)`)**. That's why the pointers you get tend to end in `...010`, `...020`, `...030`,
and so on, rather than `...000`.

Consequences:

1. **`free(p)` reads the header at `p - 16`** to learn the chunk size `[GLIBC-malloc.c]`. If you
   pass `free` a pointer that didn't come from `malloc`, such as a pointer you moved forward to
   align it, `free` reads garbage as a size. Part 3 §7 shows ASan catching that, and Part 7
   explains why attackers care.
2. **Your 4096-byte request actually used more memory.** `malloc_usable_size` reported **4104**
   **(measured)**: the chunk was rounded to a multiple of 16 that is big enough, plus header
   overlap **(derived)**. Allocators always trade memory for speed and alignment.

## 6. Big allocations: the mmap threshold (a nasty surprise)

**(measured)**:

```
malloc(     4096) = 0x5ae5849d3020  %4096=  32
malloc(   100000) = 0x5ae5849d5040  %4096=  64
malloc(   200000) = 0x7a19ed2d9010  %4096=  16
malloc(  1048576) = 0x7a19ecaff010  %4096=  16
malloc( 67108864) = 0x7a19e8bff010  %4096=  16
vector<char>(1MiB).data() %4096=64
mmap = 0x7a19ed34d000 %4096=0
```

Two different address regions appear: `0x5ae5…` (the classic heap) and `0x7a19…`. `[MAN-mallopt]`
explains it:

> "For allocations greater than or equal to the limit specified (in bytes) by M_MMAP_THRESHOLD
> that can't be satisfied from the free list, the memory-allocation functions employ mmap(2)
> instead of increasing the program break using sbrk(2)." ... "a default setting of 128*1024 for
> the M_MMAP_THRESHOLD parameter." (It is also *dynamic*: it grows when big blocks are freed.)

`mmap` always returns **page-aligned** memory `[MAN-mmap]` ("the kernel chooses the
(page-aligned) address"). But glibc then puts its 16-byte chunk header **at the start of that
page** and returns `page + 16` **(derived from §5 + the measured `%4096=16`)**.

So a large `malloc` is **always** at offset 16 within a page, which means it is **never**
512-aligned. "Just allocate a big buffer, it'll be page-aligned" is wrong, deterministically. The
1 MiB `vector` was at +64 because that run happened to come from the heap after the dynamic
threshold moved. Either way, alignment is not guaranteed.

## 7. `operator new` and `std::vector`: the same promise

C++17 added a macro that states the promise of `operator new(std::size_t)` `[P0035]`:

> "`__STDCPP_DEFAULT_NEW_ALIGNMENT__` — An integer literal of type size_t whose value is the
> alignment guaranteed by a call to operator new(std::size_t)."

On your VM it is **16** **(measured)**, the same as `malloc`, because libstdc++'s
`operator new` calls `malloc` **(derived from matching numbers; confirm by stepping into
`operator new` with gdb, Drill 3)**.

The chain for your drill's buffer is **(derived)**:

```
std::vector<char> buffer(4096, 'x');
  → std::allocator<char>::allocate(4096)
    → ::operator new(4096)                      // alignof(char) = 1 ≤ 16, so the plain overload
      → malloc(4096)                            // promise: 16-aligned
        → returned 0x...a020                    // natural alignment 32 (luck: 16 promised)
```

`std::vector<char>` has no way to ask for more. Its allocator is `std::allocator<char>`, and `char`
has alignment 1. The vector does exactly what its contract says. **The contract was the wrong
one for O_DIRECT.** Part 4 builds a vector whose allocator has the right contract.

One more C++17 rule `[CPPREF-operator_new]`: if a **type** is over-aligned (for example
`struct alignas(64) Line`), `new Line` automatically calls `operator new(size, std::align_val_t{64})`
because 64 > `__STDCPP_DEFAULT_NEW_ALIGNMENT__`. **(measured)**: `new alignas(64)[2] -> %64=0`.
Before C++17 that silently returned 16-aligned memory. `[P0035]` opens with exactly this bug:

```cpp
class alignas(16) float4 { float f[4]; };
float4 *p = new float4[1000];   // pre-C++17: "implementations were essentially required to allocate memory incorrectly"
```

So one way to get an aligned buffer from `new` is to give the **element type** the alignment
(`struct alignas(512) Sector { char b[512]; };`). Part 3 §4 shows this.

## 8. "It worked on my machine": alignment by luck

There are three ways luck can hide this bug from you, and you've now seen all three:

| Luck | Probability / condition | Source |
|---|---|---|
| heap pointer happens to be 512-aligned | ~3 % per allocation | §4 **(measured)** |
| file lives on tmpfs (e.g. `/tmp`) | 100 % — tmpfs accepts misaligned O_DIRECT on your kernel | Part 5 §6 **(measured)** |
| device/filesystem has a smaller requirement | e.g. NFS client: "places no alignment restrictions" | `[MAN-open]` |

A systems engineer's reflex: **when something depends on alignment, assert it explicitly at the
point of use** (`assert(addr % align == 0)`). RocksDB does exactly that before every direct
`pread`: `assert(IsSectorAligned(scratch, GetRequiredBufferAlignment()));`
`[ROCKS-SRC:io_posix.cc]`. Exercise 5 has you turn the vague `EINVAL` into an explicit,
named error.

## 9. Summary table

| Storage | Who picks the address | Alignment guaranteed (your VM) | Can you ask for more? |
|---|---|---|---|
| global / `static` | compiler + linker | `alignof(T)` | yes: `alignas(N)` |
| local (stack) | compiler | `alignof(T)`; stack pointer 16 at calls | yes: `alignas(N)` (costs a stack realign) |
| `malloc` / `calloc` / `realloc` | glibc | 16 (`max_align_t`) | no → use `aligned_alloc` / `posix_memalign` (Part 3) |
| `malloc` ≥ mmap threshold | glibc via `mmap` | 16, and **always** page+16 | no |
| `new T` / `new T[n]` | `operator new` | `max(alignof(T), 16)`; over-aligned T → aligned overload (C++17) | yes: over-aligned type, or `operator new(n, align_val_t)` |
| `std::vector<char>` | `std::allocator<char>` → `operator new` | 16 | yes: custom allocator (Part 4) |
| `mmap` | kernel | page (4096) | larger: over-map and trim, or huge pages |

## 10. Drills

1. Re-run the §3 program 5 times. Does the histogram change? Does the 16/32/64 halving pattern
   hold? Run it with `MALLOC_MMAP_THRESHOLD_=0 ./prog` (glibc tunable env var; see
   `[MAN-mallopt]`) and explain the new histogram using §6.
2. Allocate `malloc(24)`, `malloc(25)`, `malloc(40)` and print `malloc_usable_size` for each.
   Derive the rounding rule from your numbers.
3. In gdb: `break operator new`, `run`, `step` until you reach `malloc`. Write down the call
   chain you saw.
4. Compile `void f(){ alignas(512) char b[512]; g(b); }` with `g++ -O2 -S` and find the
   instruction that aligns the stack. Explain it using Part 1 §8 (align-down).
5. Why is relying on "large mallocs come from mmap, which is page-aligned" wrong even if glibc
   didn't add a header? (Hint: the threshold is dynamic, and other allocators like jemalloc or
   tcmalloc behave differently.)

## My summary

