# 1.4 — Conversions, Promotions, and the Signed/Unsigned Trap

Most integer bugs don't happen in the arithmetic — they happen in the **invisible conversions** the
compiler inserts around it. This section makes those conversions visible.

## 1. Four kinds of implicit integer conversion

When integers of different types meet, C++ converts them by rules you don't see in the source:

1. **Integral promotion** — small types (`char`, `short`, `bool`) are promoted to `int` before
   almost any operation (§2).
2. **The usual arithmetic conversions** — the two operands of `+ - * / < == …` are brought to one
   common type (§3). This is where signed meets unsigned.
3. **Integral conversion on assignment / narrowing** — storing a wider value into a narrower type
   (§4).
4. **Conversion to/from `bool`** (nonzero → true). Minor; noted for completeness.

## 2. Integral promotion

`[STD-conv.prom]`/2:

> "A prvalue ... that has an integer type other than bool, char8_t, char16_t, char32_t, or wchar_t
> whose integer conversion rank is less than the rank of int can be converted to a prvalue of type
> int if int can represent all the values of the source type; otherwise ... to a prvalue of type
> unsigned int."

In practice: a `char` or `short` is promoted to `int` the moment you do arithmetic on it. So
`a_char + a_char` has type `int`, not `char`. This is why §1.1's `std::byte` deliberately has **no**
`+` — to avoid silent promotion of raw bytes to signed `int`. It's also why `uint16_t a,b;
a * b` can be surprising: both promote to `int`, the multiply is **signed `int`**, and a large
product can signed-overflow (UB) even though you were "only using unsigned 16-bit values". (A real
CVE class; see §1.6.)

## 3. The usual arithmetic conversions — the dangerous one

When the two operands differ, `[STD-expr.arith.conv]`/1 picks a common type `C`. After promotion,
for two integer operands with one unsigned (U) and one signed (S):

> "(1.5.3.1) If U has rank greater than or equal to the rank of S, C is U.
> (1.5.3.2) Otherwise, if S can represent all of the values of U, C is S.
> (1.5.3.3) Otherwise, C is the unsigned integer type corresponding to S."

Rule 1.5.3.1 is the trap: **if the unsigned type is at least as wide, the signed operand is
converted to unsigned.** A negative value becomes huge (via the mod-2^N rule, `[STD-conv.integral]`).

**(measured)**, the canonical example:

```cpp
int x = -1;
unsigned y = 1;
(x < y)  →  false        // because -1 converts to 4294967295, and 4294967295 < 1 is false
```

Read that again: **`-1 < 1` is `false`** when the `1` is unsigned and the same width. The comparison
you wrote isn't the comparison that ran. Compilers warn about this under `-Wsign-compare` (part of
`-Wextra`), which is why every exercise in this course builds with `-Wextra -Wconversion`.

## 4. Narrowing and the `size()-1` bug

Storing a value into a narrower (or differently-signed) type keeps it only modulo 2^N
`[STD-conv.integral]`/3: "the unique value of the destination type that is congruent to the source
integer modulo 2^N". No error, at most a warning.

The most common real instance comes straight from `[ALGO]`'s binary-search skeleton:

```cpp
int high = v.size() - 1;      // v is a std::vector
```

`v.size()` is `std::size_t` (unsigned, 64-bit). The subtraction happens in **unsigned**. If `v` is
empty, `size()` is `0`, and `0 - 1` **wraps to 18446744073709551615** (§1.3), which is then
narrowed into `int high` — **(measured)** it lands as `−1`. Now `while (low <= high)` with
`high == −1`... or, in an unsigned loop, `for (size_t i = 0; i <= v.size()-1; ++i)` runs ~1.8×10¹⁹
times and walks off the end. Either way you've built an out-of-bounds access out of an *empty*
container. `[ALGO §… binary search]` writes `int high = v.size() - 1;` exactly like this — correct
only because its inputs are non-empty; in your own code, guard the empty case or keep the index
type unsigned and test `i < size()` (never `i <= size()-1`).

## 5. Defensive conversion rules (use these in the exercises)

1. **Pick the right type at the source.** Sizes and indices into containers: `std::size_t`.
   Differences and quantities that can be negative: a signed type. Don't mix without thinking.
2. **Compare same-signedness.** If you must compare signed and unsigned, convert explicitly after
   checking the sign, or (C++20) use `std::cmp_less`, `std::cmp_equal`, … from `<utility>`, which
   give the mathematically correct answer regardless of signedness **(derived from their spec)**.
3. **Make narrowing explicit.** `static_cast<int>(x)` says "I know this truncates." Brace-init
   (`int n{x};`) *forbids* narrowing at compile time — a free safety net.
4. **Turn on the warnings.** `-Wconversion -Wsign-conversion -Wsign-compare` catch most of this
   at build time. Treat them as errors in anything you'll red-team later.

## 6. Where this lands

The byte reader in Lesson 2 must not let `offset + length` (mixed types) wrap past the buffer.
Lesson 7's allocator takes a `size_t` count and must not narrow it. The alignment allocator
(Lesson 9) had an `n > SIZE_MAX / sizeof(T)` check for exactly the §2 promotion/overflow reason.

## Drills

1. Predict and then measure: `-1 < 1u`, `-1 == 4294967295u`, `(1u - 2u) > 0`,
   `(short)-1 < 1u`, `sizeof(int) - 10` (what type? what value?).
2. Write the empty-vector binary-search bug, run it under ASan, and watch the out-of-bounds read.
   Fix it two ways (guard empty; use unsigned `i < size()`).
3. Replace a signed/unsigned comparison with `std::cmp_less` and confirm the result flips to the
   mathematically correct one.
4. Compile `uint16_t a=50000,b=50000; auto c=a*b;` with `-Wconversion -fsanitize=undefined`. What
   is `decltype(c)`? Does it overflow? Explain with §2.

## My summary
