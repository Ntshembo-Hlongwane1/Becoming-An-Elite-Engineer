# 1.3 — Overflow and Wraparound

This is the single most important section in Lesson 1 for a security engineer. The difference
between "wraps, defined" and "overflow, undefined" decides whether a bug is a predictable logic
error or a door the optimiser can turn into memory corruption.

## 1. Unsigned: wraps, and that's *defined*

`[STD-basic.fundamental]`/2: "arithmetic for the unsigned type is performed **modulo 2N**." So an
unsigned result that doesn't fit is reduced mod 2^N — it wraps around, with a guaranteed value.
**(measured)**:

```
0u - 1u        = 4294967295          (0 − 1 ≡ 2³² − 1)
4000000000u + 1000000000u = 705032704   (5·10⁹ − 2³² = 705032704)
```

This is well-defined and portable. It is *not* therefore safe: the value is often not what you
wanted. `0u - 1u` becoming ~4 billion is the engine of the `size()-1` bug in §1.4 and of countless
"allocate `len`, then read `len` bytes" exploits where `len` wrapped to something huge.

## 2. Signed: overflows, and that's *undefined behaviour*

There is no modulo rule for signed arithmetic. If a signed operation's true result doesn't fit the
type, it is **undefined behaviour (UB)**. `[STD-expr.pre]`/4:

> "The behavior of evaluating an arithmetic expression is undefined if the mathematical result is
> neither in the range of representable values for its type nor a negative infinity, positive
> infinity, or NaN ..."

UB means the standard imposes **no** requirements. The compiler may assume signed overflow *never
happens* and optimise on that assumption. **(measured)** with `-fsanitize=undefined`:

```
ov.cpp: runtime error: signed integer overflow: 2000000000 + 2000000000 cannot be represented in type 'int'
```

The same program with no sanitizer printed `−294967296` (it happened to wrap), and with `-fwrapv`
it is *defined* to wrap:

```
-fwrapv: -294967296
```

Three different behaviours from one expression. That is the hallmark of UB: "it worked" tells you
nothing.

### Why the compiler is allowed to bite

Because signed overflow is UB, a compiler may legally conclude things like `x + 1 > x` is *always
true* (it would only be false on overflow, which "can't happen"), and delete a bounds check you
wrote. That's not hypothetical. **(measured)** — these two one-liners at `-O2`, disassembled:

```cpp
bool f_signed  (int      x){ return x + 1 < x; }   // overflow is UB
bool f_unsigned (unsigned x){ return x + 1 < x; }   // overflow wraps (defined)
```
```asm
f_signed:    xorl %eax, %eax ; ret        ← compiled to "return false", UNCONDITIONALLY
f_unsigned:  cmpl $-1, %edi  ; sete %al   ← actually tests x == UINT_MAX, the real wrap case
```

The signed check was **deleted** — the compiler trusted "signed overflow never happens" and folded
`x+1 < x` to `false`. Had that been your overflow guard before a `malloc`, the guard is now gone and
the allocation proceeds. The unsigned version, being defined, was kept and computed correctly. This
is *the* reason you never rely on signed overflow to detect itself. The flags that change the
default `[GCC-fwrapv]`:

| Flag | Effect on signed overflow |
|---|---|
| (default, `-O2`) | UB; optimiser may assume it never happens |
| `-fwrapv` | defined to wrap (two's complement), like unsigned |
| `-ftrapv` | traps (aborts) at run time on overflow |
| `-fsanitize=undefined` | detects and reports it at run time (your debugging default) |

## 3. The asymmetry bug: `-INT_MIN` and `abs(INT_MIN)`

From §1.2: `INT_MIN = −2147483648`, but `+2147483648` is not representable. So `-INT_MIN` and
`std::abs(INT_MIN)` are signed overflow (UB) **(derived)**. Classic places this hides: taking the
absolute value of a user-supplied int, or negating a parsed offset.

## 4. The book's own examples

### 4a. The safe midpoint `[ALGO §… merge sort]`

Your algorithms book writes the midpoint of a binary search / merge sort as:

```cpp
int mid = left + (right - left) / 2;      // [ALGO] mergeSort
```

not the "obvious" `(left + right) / 2`. Why **(measured)**:

```
lo=1500000000, hi=2000000000
lo + hi  (true value 3500000000) as int = -794967296   ← signed overflow, UB
lo + (hi - lo)/2                          = 1750000000  ← correct, no overflow
```

`left + right` can exceed `INT_MAX` even though the average can't. `left + (right-left)/2` never
exceeds the larger operand, so it can't overflow — **as long as `right - left` itself fits**, which
it does here because array indices are non-negative. This exact bug famously sat in
`java.util.Arrays`' binary search and the JDK for years. Memorise the safe form.

The fully general case (any `lo ≤ hi`, including opposite-sign extremes like `(INT_MIN, INT_MAX)`)
is subtler: there `right - left` *also* overflows. The overflow-free trick is to take the gap in
**unsigned**: `(unsigned)hi - (unsigned)lo` is the exact non-negative gap (mod-2^N arithmetic,
§1.2), halve it, and add back — which is what C++20's `std::midpoint` does. Lesson 1's exercise has
you implement this.

### 4b. Bellman-Ford relaxation `[ALGO §14.2.3]`

> "**Integer Overflow** — `dist[e.u] + e.w < dist[e.v]` // If dist[e.u] == INF, can overflow.
> Always check `dist[e.u] != INF` before performing additions." ... "Always guard against integer
> overflow when relaxing edges."

`INF` is usually a large sentinel like `INT_MAX`; `INF + w` overflows. The book's fix is a guard
before the add — the same pattern as checked arithmetic (§5).

## 5. Checked arithmetic: making overflow impossible to miss

Two portable tools.

### 5a. Check *before* you operate (works everywhere, pure standard)

The rule for "will `a + b` overflow an unsigned type?" is: it overflows iff `b > MAX - a`
**(derived — rearranged from `a + b > MAX`, done so the sum itself is never formed)**. This is the
glibc/alignment-lesson pattern (`bytes > SIZE_MAX - alignment`) and CERT INT30-C `[CERT-INT30]`:

```cpp
#include <limits>
bool add_would_overflow_unsigned(std::size_t a, std::size_t b) {
    return b > std::numeric_limits<std::size_t>::max() - a;   // never forms a+b
}
```

For multiply: `a != 0 && b > MAX / a` **(derived)**. For signed, the checks are fiddlier (both ends
of the range, and `INT_MIN` negation), which is why you prefer 5b for signed.

### 5b. Let the hardware tell you: `__builtin_*_overflow`

GCC/Clang expose the CPU's own overflow flag `[GCC-overflow]`:

> "promote the first two operands into infinite precision ... perform [the operation] on those
> promoted operands. The result is then cast to the type the third pointer argument points to and
> stored there. If the stored result is equal to the infinite precision result, the built-in
> functions return `false`, otherwise they return `true`."

```cpp
std::size_t out;
if (__builtin_mul_overflow(n, sizeof(T), &out)) { /* refuse: would overflow */ }
// else `out` holds n*sizeof(T), guaranteed correct
```

This is exactly how a careful allocator computes `n * sizeof(T)` (Lesson 7, and the alignment
allocator in Lesson 9). C++26 adds standard `<numeric>` functions (`std::add_sat`/saturating, and
`std::ckd_add`-style checked ops arriving via `<stdckdint.h>`); until then the builtins are the way.
Your Lesson-1 exercise implements both the pre-check (5a) and a builtin-based path so you feel the
difference.

## 6. Where this lands in the course

- Lesson 2: a byte reader that must not let a length wrap past the buffer end.
- Lesson 7: `malloc`'s size class math and your own allocator's request size.
- Lesson 9 (done): `AlignUp` refusing to wrap (CVE-2013-4332).
- Lesson 17: these overflows are the *root cause* whose *effect* is the heap/stack overflow.

## Drills

1. Reproduce the three behaviours of `2e9 + 2e9` (plain, `-fwrapv`, `-fsanitize=undefined`).
   Explain each with this section.
2. Write `mul_would_overflow(size_t a, size_t b)` with the pre-check, and a second version with
   `__builtin_mul_overflow`. Fuzz them against each other with random inputs for 1e6 iterations;
   they must always agree.
3. Show that `(left + right)/2` overflows for `left = right = 2e9` but `left + (right-left)/2`
   doesn't. Print both.
4. Compile a function containing `if (x + 1 < x) abort();` at `-O2` and look at the assembly
   (`g++ -O2 -S`). Did the compiler keep the check for `int x`? For `unsigned x`? Explain.

## My summary
