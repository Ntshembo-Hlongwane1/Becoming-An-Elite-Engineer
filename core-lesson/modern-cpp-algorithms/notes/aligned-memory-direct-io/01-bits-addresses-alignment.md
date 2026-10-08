# Part 1 — Bits, Addresses, and Alignment From Zero

> Before you can align anything you need to see an address the way the hardware sees it: as a
> row of bits. Everything in this lesson — `% 512`, `& 511`, "ends in `000`", `EINVAL` — is a
> statement about the **lowest bits of a number**. Companion reading: `[MEM §3.1–3.2]`
> (pointers, pointer arithmetic), `[ALGO §22.1]` (cache lines), `[LSM-P2 §2–3]`.

Contents

1. Bits and bytes
2. Hexadecimal — why every address is printed in hex
3. Memory is an array of bytes; an address is an index
4. Pointers vs integers: `std::uintptr_t`
5. Powers of two in binary
6. Divisibility by a power of two = "the low bits are zero"
7. The mask trick: `x % N == x & (N - 1)`
8. Align **down** and align **up** (and the overflow hiding in align-up)
9. Testing "is this a power of two?"
10. Alignment in C++: `alignof`, `alignas`, fundamental vs extended
11. Why hardware wants alignment
12. Misaligned access in C++ is undefined behaviour (with UBSan proof)
13. Padding: how alignment shapes `struct` layout
14. Your vector, bit by bit
15. Drills

---

## 1. Bits and bytes

- A **bit** is one binary digit: `0` or `1`.
- A **byte** is 8 bits. It can hold 2⁸ = 256 different patterns, `00000000` to `11111111`
  (0 to 255).
- In C++, `char` is exactly one byte (`sizeof(char) == 1` by definition) `[CPPREF-object]`.

A number written in **binary** uses powers of two for its digit positions, exactly like decimal
uses powers of ten:

```
decimal 13  = 1·8 + 1·4 + 0·2 + 1·1  = binary 1101
position:     2³    2²    2¹    2⁰
```

The rightmost bit (2⁰) is the **lowest** or **least significant** bit. "The low 9 bits" means
positions 2⁰ … 2⁸.

## 2. Hexadecimal — why every address is printed in hex

Binary is long. **Hexadecimal** (base 16) uses digits `0–9 a–f` (a=10 … f=15). One hex digit is
exactly **4 bits**, because 16 = 2⁴ **(derived)**:

| hex | binary | | hex | binary |
|---|---|---|---|---|
| 0 | 0000 | | 8 | 1000 |
| 1 | 0001 | | 9 | 1001 |
| 2 | 0010 | | a | 1010 |
| 3 | 0011 | | b | 1011 |
| 4 | 0100 | | c | 1100 |
| 5 | 0101 | | d | 1101 |
| 6 | 0110 | | e | 1110 |
| 7 | 0111 | | f | 1111 |

In C++ a hex literal starts with `0x`. So you can convert hex to binary **digit by digit**, with no
arithmetic:

```
0x1000 = 0001 0000 0000 0000 (binary) = 2¹² = 4096
0x0200 = 0000 0010 0000 0000          = 2⁹  = 512
0x0020 = 0000 0000 0010 0000          = 2⁵  = 32
```

That's why memory addresses are printed in hex: the bit structure stays visible.

## 3. Memory is an array of bytes; an address is an index

Picture your process's memory as one enormous `unsigned char mem[...]`. The **address** of a
byte is its index in that array. A **pointer** is a variable whose value is an address.

Two refinements, so you don't carry a wrong picture forward:

1. These are **virtual** addresses. Each process gets its own private address space; the CPU and
   kernel translate virtual addresses to physical RAM one **page** (4096 bytes on your VM) at a
   time `[OSTEP-13]`, `[KERNEL-MM]`. Translation keeps the low 12 bits (the offset inside a page)
   unchanged **(derived from the page size: 4096 = 2¹²)**. So alignments up to 4096 that you
   observe on virtual addresses also hold for the physical addresses. That matters in Part 5,
   because the disk's DMA engine works on physical memory.
2. On x86-64 Linux, user-space addresses look like `0x5ae5849d3020` or `0x7a19ed2d9010`. They're
   just 64-bit numbers with the high bits mostly zero. **(measured, Part 2)**

## 4. Pointers vs integers: `std::uintptr_t`

You cannot do `%` or `&` on a pointer in C++. Pointer arithmetic (`p + 3`) moves by *elements*,
not bytes, and only within one array `[MEM §3.2]`. To ask questions about the **number** inside a
pointer, convert it to an unsigned integer that is big enough:

```cpp
#include <cstdint>                                   // (1)

char* p = some_buffer;
std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(p);   // (2)
bool aligned512 = (addr % 512) == 0;                          // (3)
```

1. `<cstdint>` declares fixed-width integer types, including `std::uintptr_t`.
2. `std::uintptr_t` is "unsigned integer type capable of holding a pointer to void"
   `[CPPREF-uintptr_t]`. `reinterpret_cast` turns the pointer into that integer. This is the one
   legitimate way to look at the address bits. (cppreference marks the type *optional*; it
   exists on every platform you'll use, and GCC on x86-64 provides it as a 64-bit unsigned type.)
3. Now ordinary integer arithmetic works on the address.

`void*` → `uintptr_t` → `void*` round-trips to the same pointer `[CPPREF-uintptr_t]`. Converting
an *arbitrary* integer back to a pointer and using it is how you create invalid pointers, so do it
only for addresses that came from a real pointer.

## 5. Powers of two in binary

A power of two has **exactly one bit set**:

```
   1 = 2⁰  = 0b0000000000001
   8 = 2³  = 0b0000000001000
 512 = 2⁹  = 0b0001000000000
4096 = 2¹² = 0b1000000000000
```

Subtract one and every bit **below** that single bit becomes 1, and the bit itself becomes 0
**(derived: like 1000 − 1 = 999 in decimal)**:

```
 512     = 0b0001000000000
 512 - 1 = 0b0000111111111   (= 511, nine ones)
```

`N - 1` for a power-of-two N is called the **mask**. It has exactly log₂N ones at the bottom.

## 6. Divisibility by a power of two = "the low bits are zero"

Claim: **x is a multiple of 2ᵏ exactly when the lowest k bits of x are all zero.**

Why **(derived)**: every bit at position ≥ k contributes a multiple of 2ᵏ (2ᵏ, 2ᵏ⁺¹, …). The
bits below k contribute 0 to 2ᵏ−1, which is less than 2ᵏ. So x mod 2ᵏ is *exactly* the number
formed by the low k bits. It's zero only if those bits are all zero.

In hex that becomes a rule you can apply by eye:

| N | k = log₂N | "x is N-aligned" means… |
|---|---|---|
| 16 | 4 | last hex digit is `0` |
| 256 | 8 | last two hex digits are `00` |
| 512 | 9 | last two hex digits `00` **and** the third-last digit even |
| 4096 | 12 | last three hex digits are `000` |

## 7. The mask trick: `x % N == x & (N - 1)`

`&` is **bitwise AND**: each output bit is 1 only if both input bits are 1. ANDing with the mask
(`N - 1`, all ones in the low k bits) **keeps the low k bits and clears the rest**. By §6, the
low k bits *are* `x % N`. So for power-of-two N **(derived)**:

```
x % N  ==  x & (N - 1)
```

Worked example. The address your vector got was `...a020`:

```
x        = 0x...a020 = ...1010 0000 0010 0000
N - 1    = 0x01ff    = ...0000 0001 1111 1111     (511)
x & 511  =             ...0000 0000 0010 0000  = 0x20 = 32
```

So the vector started 32 bytes past a 512 boundary.

Why bother when `%` exists? `%` compiles to a division instruction, which is slow (tens of cycles),
unless the compiler knows N at compile time. `&` takes one cycle. Allocators and storage engines
do this on every request, and that is why LevelDB's arena uses
`current_mod = reinterpret_cast<uintptr_t>(alloc_ptr_) & (align - 1)` `[LDB-SRC:arena]`. The
trick only works for powers of two. With N = 3000, `x & 2999` is meaningless.

## 8. Align **down** and align **up**

These two formulas appear in every allocator, every storage engine, and Exercise 1.

### Align down: largest multiple of N that is ≤ x

```
align_down(x, N) = x & ~(N - 1)
```

- `~` is **bitwise NOT**: it flips every bit. `~(N-1)` is all ones *except* the low k bits.
- ANDing x with it **clears the low k bits**, which drops x to the multiple of N at or below it.

```
x              = 5000 = 0b1 0011 1000 1000
~(4096-1)      =        ...1 0000 0000 0000
x & ~(4095)    = 4096
```

RocksDB calls this `TruncateToPageBoundary`: `s -= (s & (page_size - 1));`
`[ROCKS-SRC:aligned_buffer.h]`. It subtracts the remainder, which gives the same result.

### Align up: smallest multiple of N that is ≥ x

```
align_up(x, N) = (x + N - 1) & ~(N - 1)
```

Line by line **(derived)**:

- If x is already a multiple of N, then `x + N - 1` is still below the *next* multiple, so
  rounding down gives x back. ✔
- If x isn't a multiple, adding `N - 1` pushes it **past** the next multiple but not past the one
  after, so rounding down lands exactly on the next multiple. ✔

```
x = 5000, N = 4096:  5000 + 4095 = 9095 → & ~4095 = 8192   ✔ (next multiple)
x = 4096, N = 4096:  4096 + 4095 = 8191 → & ~4095 = 4096   ✔ (unchanged)
x = 0,    N = 4096:     0 + 4095 = 4095 → & ~4095 = 0      ✔
```

The equivalent division form, used by RocksDB's `Roundup`, is `((x + N - 1) / N) * N`
`[ROCKS-SRC:aligned_buffer.h]`. It also works for N that isn't a power of two, at the cost of a
division.

### ⚠ The overflow hiding in align-up

`std::size_t` is 64-bit unsigned, so arithmetic **wraps around** at 2⁶⁴. If x is close to
`SIZE_MAX`, then `x + N - 1` wraps to a *small* number, and align-up returns something tiny
**(derived)**:

```
x = SIZE_MAX - 10, N = 4096
x + 4095 wraps → 4084   →  & ~4095  →  0
```

An allocator that does this then hands out a **0-byte** block for a request of almost 2⁶⁴ bytes,
and the caller writes past the end. That's a heap overflow. It is exactly the bug class behind
**CVE-2013-4332** in glibc's `memalign`/`aligned_alloc` family. The fix added the guard
`if (bytes > SIZE_MAX - alignment - MINSIZE)` before the arithmetic `[GLIBC-15857-FIX]`. Part 7
covers it in depth. Exercise 1 makes you write the check.

The safe pattern is to check before you add:

```cpp
if (x > SIZE_MAX - (N - 1)) { /* would overflow: refuse */ }
```

## 9. Testing "is this a power of two?"

```cpp
bool is_pow2(std::size_t x) { return x != 0 && (x & (x - 1)) == 0; }
```

- `x & (x - 1)` **clears the lowest set bit** of x **(derived from §5: subtracting 1 flips the
  lowest 1 to 0 and the zeros below it to 1; the AND then zeroes all of those)**.
- A power of two has only one set bit, so the result is 0.
- `x != 0` is needed because `0 & (0 - 1)` is also 0, and 0 is not a power of two.

C++20 provides this ready-made as `std::has_single_bit(x)` in `<bit>`. It also has
`std::bit_ceil(x)` (round up to a power of two) and `std::countr_zero(x)` (number of trailing zero
bits, which is how many low bits are zero, i.e. the address's natural alignment) `[CPPREF-bit]`.

## 10. Alignment in C++

From cppreference `[CPPREF-object]`:

> "Every object type has the property called *alignment requirement*, which is a nonnegative
> integer value (of type std::size_t, and always a power of two) representing the number of bytes
> between successive addresses at which objects of this type can be allocated."

In plain words: if `alignof(int) == 4`, an `int` may only live at addresses that are multiples of 4.

| Tool | Meaning | Source |
|---|---|---|
| `alignof(T)` | the alignment requirement of type T | `[CPPREF-alignof]` |
| `alignas(N)` on a variable or member | "this object must be N-aligned" | `[CPPREF-alignas]` |
| `alignas(N)` on a struct | every object of this type is N-aligned, and `sizeof` is rounded up to a multiple of N | `[CPPREF-alignas]` |
| `std::max_align_t` | a type whose alignment is the largest **fundamental** alignment | `[CPPREF-max_align_t]` |

Fundamental vs extended `[CPPREF-object]`: "the largest *fundamental alignment* of scalar type
is implementation-defined and equal to the alignment of std::max_align_t." Anything stricter is
**extended alignment**, and a type with it is **over-aligned**. A 512- or 4096-byte I/O buffer is
over-aligned. That word is why Part 2 matters: ordinary allocators only promise *fundamental*
alignment.

Values on your VM **(measured — program in Part 2 §3)**:

```
alignof(char)=1 int=4 double=8 long double=16
alignof(max_align_t)=16
```

## 11. Why hardware wants alignment

There are three different consumers, and each one has its own reason.

1. **The CPU's caches.** Memory moves between RAM and the CPU in **cache lines**: 64 bytes on your
   CPU **(measured, `coherency_line_size`)**, and `[ALGO §22.1.1]` gives the same "typically 64
   bytes" figure. An 8-byte value at an 8-aligned address can never straddle two lines; at
   `...3c` it would span two lines and need two fetches **(derived: 0x3c + 8 = 0x44 crosses
   0x40)**. `[ALGO §22.1.6]` and `[MEM §17.5]` recommend `alignas(64)` / `std::aligned_alloc`
   for this reason.
2. **Atomicity and some CPU architectures.** Some CPUs fault on misaligned loads, and atomic
   operations on misaligned data are not guaranteed to be atomic. C++ avoids the whole question by
   making misalignment undefined (§12).
3. **DMA engines and storage devices.** A disk transfers whole **sectors** (512 bytes on your
   virtual disk, **measured with `lsblk`**). With `O_DIRECT` the device moves data straight
   between your buffer and the sector, so the buffer, the length and the file offset must line up
   with that granularity `[MAN-open]`, `[ROCKS-DIO]`. This is your bug, and Part 5 covers it in
   detail.

Note what the three requirements have in common: each one is "the low k bits must be zero" for
some k (2–4 for scalars, 6 for cache lines, 9 for your disk, 12 for pages).

## 12. Misaligned access in C++ is undefined behaviour

`[CPPREF-object]`: "Attempting to create an object in storage that does not meet the alignment
requirements of the object's type is undefined behavior."

**(measured)**, built with `-fsanitize=address,undefined`:

```cpp
char buf[16] = {};                                             // (1)
std::uint32_t* p = reinterpret_cast<std::uint32_t*>(buf + 1);  // (2)
*p = 7;                                                        // (3)
std::printf("%u\n", *p);                                       // (4)
```

1. 16 bytes on the stack. `char` has alignment 1, so `buf` may be anywhere.
2. `buf + 1` is one byte past `buf`. If `buf` happens to be 4-aligned, `buf + 1` certainly isn't.
   The cast compiles: `reinterpret_cast` checks nothing.
3. This store creates/accesses a `uint32_t` (alignment 4) at a misaligned address, which is UB.
4. So is this load.

UBSan output on your VM:

```
ub.cpp:13:102: runtime error: store to misaligned address 0x77bf121f0061 for type 'uint32_t', which requires 4 byte alignment
ub.cpp:13:118: runtime error: load of misaligned address 0x77bf121f0061 for type 'uint32_t', which requires 4 byte alignment
```

Look at the address: `...0061`, low bits `0001`, which is not a multiple of 4. The program still
printed `7`, because x86 hardware tolerates misaligned scalar loads. **"It worked" is not "it is
correct"**: UB means the compiler may assume it never happens, and it may vectorise or reorder on
that assumption. Use `std::memcpy` to read an integer out of arbitrary bytes (`[LSM-P2 §5]`).

## 13. Padding: how alignment shapes `struct` layout

Because every member must sit at a multiple of its own alignment, the compiler inserts **padding**
bytes `[CPPREF-object]`:

```cpp
struct X {
    int n;  // size: 4, alignment: 4
    char c; // size: 1, alignment: 1
    // three bytes of padding bits
}; // size: 8, alignment: 4
```

The 3 trailing bytes exist so that in an array `X a[2]`, `a[1].n` lands at offset 8, a multiple
of 4 **(derived)**. The rules **(derived from `[CPPREF-object]`)**:

1. `alignof(struct)` = the largest member alignment.
2. Each member's offset is rounded **up** to its alignment, which is align-up from §8.
3. `sizeof(struct)` is rounded **up** to a multiple of `alignof(struct)`.

With `alignas`:

```cpp
struct alignas(64) Line { char c; };   // sizeof(Line) == 64, alignof == 64   (measured)
```

One meaningful byte plus 63 padding bytes. That is the standard way to keep two hot counters on
separate cache lines (to avoid **false sharing**, `[ALGO §22.1.6]`, `[LSM-P2 §14]`), and it is
also exactly how PostgreSQL makes an I/O-aligned page buffer `[PG-c.h]`:

```c
typedef struct PGIOAlignedBlock {
    alignas(PG_IO_ALIGN_SIZE) char data[BLCKSZ];   // PG_IO_ALIGN_SIZE = 4096
} PGIOAlignedBlock;
```

Padding bytes are **not reliably initialised**. If you write a struct, or an aligned buffer padded
up to a block size, to disk, whatever was in those bytes goes to disk too. Part 7 shows this as an
information leak, and `[LSM-P2 §3]` measured it on `[ALGO §11.2.2]`'s `DiskNode`.

## 14. Your vector, bit by bit

From the run in chat **(measured)**:

```
vector         0x64d989f1a020  addr % 4096 =   32
aligned_alloc  0x64d989f1c000  addr % 4096 =    0
```

Low 12 bits of each address (last 3 hex digits):

```
vector:        0x020 = 0000 0010 0000     → lowest set bit is 2⁵ → naturally 32-aligned, NOT 512-aligned
aligned_alloc: 0x000 = 0000 0000 0000     → at least 4096-aligned
```

The **natural alignment** of an address is the largest power of two dividing it, which is
`1 << std::countr_zero(addr)` **(derived from §6 + `[CPPREF-bit]`)**. Your vector's buffer was
32-aligned. The kernel needed 512 (Part 5). 32 < 512, so the result was `EINVAL`.

Part 2 explains *why* the allocator gave you 32, and why it will give you something different on
the next run.

## 15. Drills

1. Convert by hand to binary, then state the natural alignment: `0x7ffd1c40`, `0x1000`, `0x2a8`.
2. Without a calculator: `align_up(1000, 512)`, `align_down(1000, 512)`, `align_up(1024, 512)`,
   `align_up(1, 4096)`. Then verify in C++.
3. Write `bool is_aligned(const void* p, std::size_t n)` using §4 and §7. Test it on
   `alignas(64) char a[64];` with `a`, `a + 1`, `a + 64`.
4. Find an `x` for which `(x + 511) & ~511` returns 0 but `x != 0`. Explain in one sentence why
   this is a security bug.
5. Predict `sizeof` and every member offset of
   `struct S { char a; double b; char c; int d; };`, then check with `offsetof`. Reorder the
   members to minimise `sizeof`. Why does the compiler not reorder them for you? (Hint:
   `[CPPREF-object]` + the C++ rule that members are laid out in declaration order.)
6. Compile §12's program with and without `-fsanitize=undefined`. Then compile it with `-O2` and
   look at the assembly (`g++ -S`). Did the compiler emit anything special for the misaligned
   store?

## My summary

