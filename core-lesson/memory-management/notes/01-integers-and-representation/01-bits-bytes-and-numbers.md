# 1.1 — Bits, Bytes, and Unsigned Numbers

> If you studied the alignment lesson, the first half here will be familiar — we re-derive it so
> this course stands on its own. The new material starts at §5 (`std::byte`) and runs through the
> rest of Lesson 1.

## 1. The bit

A **bit** is one binary digit: `0` or `1`. It is the smallest unit of information. Hardware stores
a bit as a physical state (a charge, a voltage, a magnetic direction); C++ never lets you touch a
lone bit directly — the smallest thing you can address is a byte (§3).

## 2. The byte

A **byte** is a fixed-size group of bits, and in C++ it is *the* unit of storage. The standard,
`[STD-intro.memory]`/1:

> "The fundamental storage unit in the C++ memory model is the byte. A byte is ... composed of a
> contiguous sequence of bits, the number of which is implementation-defined."

The number of bits in a byte is the macro `CHAR_BIT` from `<climits>`. On your VM **(measured)**:

```
CHAR_BIT=8
```

8 is near-universal on anything you will target (it was *not* always 8 historically, which is why
the standard refuses to hard-code it). An 8-bit byte holds 2⁸ = **256** different patterns.

`sizeof(T)` counts **bytes**, and `sizeof(char) == 1` **by definition** — `char` *is* one byte.
On your VM **(measured)**:

```
sizeof char=1 short=2 int=4 long=8 longlong=8 ptr=8
```

Memory is then just a sequence of bytes, each with its own number (its address),
`[STD-intro.memory]`/2:

> "The memory available to a C++ program consists of one or more sequences of contiguous bytes.
> Every byte has a unique address."

Hold onto that sentence — Lesson 2 (pointers) and Lesson 3 (address space) are both about those
addresses.

## 3. Place value: how bits become an unsigned number

Binary is base 2 the same way decimal is base 10. Each position is a power of the base; you
multiply each digit by its position's weight and add:

```
decimal 237  = 2·100 + 3·10 + 7·1          weights: 10² 10¹ 10⁰
binary  1101 = 1·8 + 1·4 + 0·2 + 1·1 = 13  weights:  2³  2²  2¹ 2⁰
```

For an **unsigned** type of width N bits, the value is the sum of `bit_i · 2^i` for i = 0…N−1. The
range is therefore `0 … 2^N − 1`. The standard says exactly this, `[STD-basic.fundamental]`/2:

> "The range of representable values for the unsigned type is 0 to 2N−1 (inclusive); arithmetic for
> the unsigned type is performed modulo 2N."

(The "modulo 2N" half is §1.3 — it's why unsigned never has "overflow", only wraparound.)

- The bit at position 0 (weight 2⁰ = 1) is the **least significant bit (LSB)** — rightmost when we
  write the number.
- The bit at position N−1 is the **most significant bit (MSB)** — leftmost.
- "The low k bits" means positions 0…k−1.

## 4. Hexadecimal

Writing 32 or 64 bits out is unreadable, so we group them. One **hex** digit (base 16) is exactly
4 bits, because 16 = 2⁴ **(derived)**. So you can convert hex↔binary one digit at a time, no
arithmetic:

```
hex:    0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
binary: 0000 0001 0010 ... 1010(10) 1011(11) ... 1111(15)

0xC0FFEE = 1100 0000 1111 1111 1110 1110
```

A C++ hex literal starts with `0x`. Two hex digits = one 8-bit byte, which is why memory dumps,
addresses and byte protocols are all written in hex. You can also write binary directly in C++14+
with `0b` (`0b1101`), and separate digits with `'` for readability (`0b1101'0110`, `0xC0FF'EE00`).

## 5. `std::byte` — a byte that is *not* a number

Here is the first genuinely new idea. C and old C++ use `char` / `unsigned char` for two completely
different jobs: (a) text, and (b) "a raw byte of storage I want to look at". Mixing them is a
source of bugs (is `+` on this thing arithmetic or concatenation? is it signed?).

C++17 added `std::byte` (in `<cstddef>`) for job (b) only. From its definition it is a scoped
enumeration over `unsigned char`, and the standard gives it **only bitwise operators**
(`&, |, ^, ~, <<, >>`) — no `+`, no `-`, no implicit conversion to int. It says, in the type
system, "I am storage, not a quantity." **(measured)**:

```cpp
#include <cstddef>
std::byte b{0xC0};            // brace-init from an integer literal
b = b | std::byte{0x0F};      // bitwise OK  -> 0xCF
int n = std::to_integer<int>(b);   // explicit, deliberate conversion to a number
// b = b + 1;                 // ERROR: no operator+ for std::byte  (that's the point)
```

Why a systems engineer cares: when you write a buffer type (Lesson 2's exercise, the alignment
lesson's `AlignedBuffer`), using `std::byte*` instead of `char*` makes the compiler stop you from
accidentally doing text/arithmetic operations on raw storage. `char` stays for actual characters.

> **Rule of thumb:** `char` for text, `unsigned char`/`std::byte` for raw bytes, a fixed-width type
> (`std::uint32_t`, §1.2) for numbers you serialize. Never `char` for a small number — its
> signedness is implementation-defined (§1.2).

## 6. The fixed-width types

`int`, `long`, etc. have only *minimum* widths in the standard, and their real widths vary by
platform (`[STD-basic.fundamental]`, Table 14: `int` ≥ 16 bits, `long` ≥ 32, `long long` ≥ 64 —
your VM has `int`=32, `long`=64). For memory work you usually want an **exact** width. `<cstdint>`
gives `std::int8_t … std::int64_t` and `std::uint8_t … std::uint64_t`, each exactly that many bits
`[CPPREF-types]`. Also:

- `std::size_t` — unsigned, big enough to hold any object's size; the type of `sizeof` and of every
  container's `.size()`. 64-bit on your VM. **This type's unsignedness is behind half the bugs in
  §1.4.**
- `std::uintptr_t` — unsigned, big enough to hold a pointer's bits (Lesson 2).
- `std::ptrdiff_t` — signed, the type of a pointer difference.

## 7. Why this matters for the rest of the course

Every size passed to `malloc`, every length in a file header, every array index, every pointer
offset is one of these integer types. The allocator in Lesson 7 computes chunk sizes; the
serialization in Lesson 2 writes fixed-width fields; the page math in Lesson 4 rounds addresses.
If you are fuzzy about "what is 2³² − 1 and what happens at that boundary", those lessons will bite
you. §1.2–1.4 nail the boundary down.

## Drills

1. By hand: convert `0b1011'0110` to hex and to decimal. Convert `0xFF` and `0x100` to decimal and
   say how many bits each needs.
2. Print `sizeof` and `CHAR_BIT * sizeof` (the bit width) for `char, short, int, long, long long,
   std::size_t, void*` on your machine. Compare with `[STD-basic.fundamental]` Table 14's minimums.
3. Write a `std::byte` that holds `0b1010'1010`, flip its low nibble with a bitwise op to get
   `0b1010'0101`... actually work out what mask does that, then check in code.
4. Try to write `std::byte{1} + std::byte{1}`. Read the error. Then do it the intended way with
   `std::to_integer`.

## My summary
