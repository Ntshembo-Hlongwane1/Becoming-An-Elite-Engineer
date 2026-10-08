# Lesson 1 — Bits, Bytes & Integer Representation

> Status: complete. Exercise: `../../exercises/01-integers/`.
>
> Memory management is the management of **bytes**, and a byte holds a **number**. Before we can talk
> about addresses, allocators or pages, we have to be able to say exactly what a number *is* in
> memory, what happens when it gets too big, and what the compiler is allowed to assume. Almost
> every memory-corruption vulnerability in history starts with an integer that did something the
> programmer didn't expect — a size that wrapped, a length that went negative, a comparison that
> flipped. This lesson makes those impossible to miss.

Read in order:

1. `01-bits-bytes-and-numbers.md` — bit, byte, CHAR_BIT, hex, unsigned place value, `std::byte`.
2. `02-signed-and-twos-complement.md` — how negatives are stored, the range, why there's one extra
   negative, `std::numeric_limits`.
3. `03-overflow-and-wraparound.md` — unsigned wrap (defined) vs signed overflow (UB), what the
   compiler does with each, `-fwrapv`/`-ftrapv`/UBSan, checked arithmetic, the book's binary-search
   and Bellman-Ford pitfalls.
4. `04-conversions-and-promotions.md` — narrowing, integral promotion, the usual arithmetic
   conversions, the signed/unsigned comparison trap, `size()-1`.
5. `05-endianness-and-serialization.md` — byte order, reading an object as bytes the *safe* way,
   fixed-width encode/decode, `std::endian`, `std::byteswap`, `std::bit_cast`.
6. `06-security-view.md` — the integer-overflow bug class as the root of heap/stack overflows;
   discovery, value, defence; CERT INT30/INT32; a real CVE's mechanism (derived, not weaponised).
7. `07-glossary.md`.

Then do the exercise.

## Why this is Lesson 1 and not an afterthought

`[MEM §5.1]` opens the memory-safety chapter with a buffer overflow:

```cpp
char buffer[10];
strcpy(buffer, "This string is too long");   // Writing beyond the array bounds
```

That's the *effect*. The *cause* is almost always an integer: a length computed wrong, a size that
wrapped, an index that went negative. `[ALGO §14.2.3]` lists "Integer Overflow" as its first
"C++ Pitfall to Avoid" and says outright: **"Always guard against integer overflow."** This lesson
is that guard, built from the bit up.
