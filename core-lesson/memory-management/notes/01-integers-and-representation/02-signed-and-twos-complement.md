# 1.2 — Signed Integers and Two's Complement

## 1. The problem: where do negatives go?

An N-bit pattern is just N bits. Nothing in the bits says "negative". **Signedness is a property of
the type, not of the bits** — the same 8 bits `1111'1111` are `255` read as `unsigned char` and
`−1` read as `signed char`. So the question is: *which rule* maps bit patterns to signed values?

C++ (since C++20) mandates exactly one rule: **two's complement**. `[STD-basic.fundamental]`/1:

> "The range of representable values for a signed integer type is −2^(N−1) to 2^(N−1)−1 (inclusive),
> where N is called the width of the type."

and it shares storage with its unsigned partner, `[STD-basic.fundamental]`/3:

> "An unsigned integer type has the same object representation, value representation, and alignment
> requirements as the corresponding signed integer type."

(Before C++20, sign-magnitude and ones'-complement were also allowed. C++20 removed them; the
rationale paper is P1236/P0907. Every machine you will use is two's complement, and now the
standard agrees.)

## 2. The two's-complement rule, derived

Take the ordinary unsigned place value (Lesson 1.1 §3), but make the **top bit's weight negative**:

```
unsigned 8-bit:  bit7·2⁷ + bit6·2⁶ + ... + bit0·2⁰     (all weights positive)
signed   8-bit:  bit7·(−2⁷) + bit6·2⁶ + ... + bit0·2⁰  (top weight is −128)
```

Work a few out **(derived)**:

```
0000'0000 =  0
0000'0001 =  1
0111'1111 =  0·(−128) + 127 = 127      ← largest positive
1000'0000 =  1·(−128) + 0   = −128     ← most negative
1111'1111 =  1·(−128) + 127 = −1
1111'1110 = −2
```

Consequences you must know cold:

- **The top bit is the sign bit**: 1 ⇒ negative, 0 ⇒ non-negative. (This falls out of the rule; it
  isn't a separate flag.)
- **The range is asymmetric**: `−2^(N−1) … 2^(N−1)−1`. There is **one more negative than positive**,
  because 0 uses up a non-negative slot. For 32-bit `int` **(measured)**: `−2147483648 … 2147483647`.
- So `−INT_MIN` is **not representable** (+2147483648 doesn't fit). Negating `INT_MIN` overflows —
  a real bug source (§1.3).

## 3. Why two's complement was chosen (the elegant part)

The great property: **addition and subtraction hardware doesn't need to know or care about sign.**
The same binary adder gives the right answer for signed and unsigned operands, as long as you read
the result with the same type. Reason **(derived)**: in N-bit arithmetic everything is mod 2^N, and
−x is represented by `2^N − x`; adding `a + (2^N − b)` and keeping N bits gives `a − b`. So the CPU
has *one* `add` instruction, not a signed one and an unsigned one. (Comparisons, multiply-high and
shifts *do* differ by sign — that's §1.3 and §1.4.)

Computing −x by hand, two ways, both **(derived)**:
1. **Invert and add one**: flip every bit (`~x`), then add 1. E.g. `+5 = 0000'0101` → invert
   `1111'1010` → +1 → `1111'1011 = −5`. ✓
2. **Subtract from 2^N**: `−5 mod 256 = 251 = 1111'1011`. ✓ (same bits)

`~x + 1 == −x` is worth memorising; you'll see `(~mask + 1)` and `(x & -x)` (isolate lowest set bit,
used in the alignment lesson's `NaturalAlignment`) and they rely on it.

## 4. `std::numeric_limits` — ask, don't hard-code

Never write `2147483647` in code. Ask the type, via `<limits>` `[CPPREF-limits]`:

```cpp
#include <limits>
std::numeric_limits<int>::min();          // −2147483648
std::numeric_limits<int>::max();          //  2147483647
std::numeric_limits<unsigned>::max();     //  4294967295
std::numeric_limits<std::size_t>::max();  //  18446744073709551615 on your VM
std::numeric_limits<int>::is_signed;      // true
std::numeric_limits<int>::digits;         // 31  (value bits, excluding the sign bit)
```

`digits` is the count of **value** bits: 31 for `int` (32 total − 1 sign), 32 for `unsigned`. That
matches `[STD-basic.fundamental]`'s "width N" where the signed range is `−2^(N−1)…2^(N−1)−1`.

The C macros (`INT_MAX`, `SIZE_MAX`, …) from `<climits>`/`<cstdint>` are the same values and are
fine too; `numeric_limits` is the generic (template-friendly) form you'll use in the exercises.

## 5. Reading the same bits two ways (preview of §1.5)

```cpp
unsigned char u = 200;          // bits 1100'1000
signed char   s = (signed char)u;   // same bits, read as signed  -> −56   (measured, §1.1)
```

200 as unsigned is `1100'1000`; its top bit is 1, so as signed it's `1·(−128)+72 = −56`. The bits
never moved; only the interpretation changed. On your VM plain `char` is **signed** (measured), so
`char c = 200;` already gives −56 — which is exactly why §1.1 said never to use `char` for a small
number.

## Drills

1. By hand, 8-bit two's complement: write the bits for `−1, −128, +127, −100`. Check each with
   `0b…` literals printed as `(int)`.
2. Show on paper that `~x + 1 == −x` for `x = 0` (watch the wraparound) and `x = INT_MIN` (watch it
   break — explain using §2's asymmetry).
3. Print `numeric_limits<T>::min/max/digits/is_signed` for `int8_t, int, unsigned, size_t,
   ptrdiff_t`. Why is `digits` for `unsigned` one more than for `int`?
4. Predict `(signed char)150`, `(signed char)255`, `(unsigned char)-1`. Verify.

## My summary
