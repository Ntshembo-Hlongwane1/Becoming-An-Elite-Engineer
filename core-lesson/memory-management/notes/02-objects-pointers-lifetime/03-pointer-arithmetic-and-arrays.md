# 2.3 — Pointer Arithmetic and Arrays

## 1. Arrays and decay

An array `int a[4]` is **four contiguous `int` objects** — 16 bytes, no header, no length stored.
In most expressions the array name "decays" to a pointer to its first element: `a` becomes
`&a[0]`, an `int*`. The length is lost in that conversion — which is why C-style code passes a
separate length, and why `std::span`/`std::array` (which keep the length) are safer (§2.2 §5).

`a[i]` is *defined* as `*(a + i)`. So indexing **is** pointer arithmetic. Everything below applies to
`a[i]` too.

## 2. The scaling rule

`p + n` does **not** add `n` bytes; it adds `n` *elements*. The compiler multiplies by
`sizeof(*p)` `[MEM §3.2]`:

```cpp
int* p = a;       // suppose a is at 0x1000
p + 1;            // 0x1004  (one int = 4 bytes later)
char* c = (char*)a;
c + 1;            // 0x1001  (one char = 1 byte later)
```

`[MEM §3.2]`: "Pointers in C++ allow arithmetic operations like incrementing (ptr++) or
decrementing (ptr--), which can help navigate contiguous memory areas like arrays. However, pointer
arithmetic must be used judiciously, as improper calculations can lead to out-of-bounds access or
invalid memory references."

The difference of two pointers into the same array, `q - p`, is the number of *elements* between
them, of type `std::ptrdiff_t` (signed, Lesson 1 §1.1).

## 3. The hard rule: arithmetic is only valid *within one array*

This is the part that turns "improper calculations" into formal UB. From the standard, via
cppreference `[CPPREF-ptr-arith]`: for `P` pointing at element `i` of an array of `n` elements,
`P + j`:

> "point[s] to the i+j-th element of x if i + j is in [0, n), and are pointers past the end of the
> last element of x if i + j is n. ... **Other j values result in undefined behavior.**"

So:
- Valid targets are indices `0 … n−1`, **plus** the "one-past-the-end" position `n`.
- **One-past-the-end** is a legal pointer *value* — you can form it, store it, and compare with it
  (it's how `end()` iterators and loop bounds work). **(measured)**: forming `a + 4` for `int a[4]`
  is fine under ASan/UBSan.
- **Dereferencing one-past-the-end is UB** `[CPPREF-ptr-arith]`.
- Forming a pointer *two or more* past the end, or before the start, is UB — even if you never
  dereference it.

Two consequences people miss:
1. You cannot take `&a[n]` to mean "address of the element after the last" by indexing — `a[n]` is a
   dereference (UB). Use `a + n` (pointer arithmetic, fine) or `std::end(a)`.
2. A pointer into array A and a pointer into array B are not comparable with `<`/`-` in defined code,
   even if they happen to be adjacent in memory.

## 4. Out-of-bounds is the classic memory bug, and ASan catches it

`[MEM §1.3]` lists buffer overflows and out-of-bounds among the core issues; `[MEM §5.1]`'s stack-
and heap-overflow examples are exactly a pointer/index walking past `n`. **(measured)** — writing
one past a 4-int heap array:

```cpp
int* p = new int[4];
p[4] = 1;              // index 4 of a 4-element array: UB (one-past-end deref + write)
```
ASan: `heap-buffer-overflow`. On the stack ASan reports `stack-buffer-overflow`; for a global,
`global-buffer-overflow`. This is why every exercise builds with ASan: the bug is otherwise silent
until it corrupts a neighbour.

## 5. Where this lands
The `ByteCursor` exercise advances a position pointer through a buffer; every read must check
`pos + n <= size` **with the overflow-safe arithmetic of Lesson 1** before moving, so the cursor can
never form or dereference an out-of-range pointer. Later, the allocator (Lesson 7) does arithmetic
across its arena; the containers (Lesson 13) hand out `begin()`/`end()` built on exactly these rules.

## Drills
1. For `double d[3]` at address `X`, give the addresses of `d+0, d+1, d+3` and say which of
   `*(d+3)`, `d+3`, `d+4` are UB.
2. Reproduce the heap-overflow under ASan; then the same with a stack array and a global array.
   Record the three different ASan labels.
3. Write a loop over `a[4]` using only pointers (`for (int* it = a; it != a+4; ++it)`). Why is
   `it != a+4` well-defined but `*(a+4)` not?
4. Measure `(&a[3] - &a[0])` and its type. Then explain why subtracting pointers into two different
   arrays is UB.

## My summary
