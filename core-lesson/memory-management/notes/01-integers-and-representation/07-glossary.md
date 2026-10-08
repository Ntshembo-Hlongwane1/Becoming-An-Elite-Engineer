# Lesson 1 — Glossary

| Term | One line | §|
|---|---|---|
| bit | one binary digit, 0 or 1 | 1.1 |
| byte | the fundamental storage unit; `CHAR_BIT` (=8) bits; `sizeof(char)==1` | 1.1 |
| `CHAR_BIT` | bits per byte, from `<climits>`; 8 on your VM | 1.1 |
| place value | value = Σ bitᵢ·2ⁱ (unsigned) | 1.1 |
| LSB / MSB | least- / most-significant bit (position 0 / N−1) | 1.1 |
| hexadecimal | base 16; one hex digit = 4 bits | 1.1 |
| `std::byte` | a raw-storage byte type with only bitwise ops, no arithmetic | 1.1 |
| fixed-width type | `int8_t…uint64_t`: exact width, for sizes/serialization | 1.1 |
| `std::size_t` | unsigned type of `sizeof`/`.size()`; 64-bit here | 1.1 |
| two's complement | signed encoding: top bit has weight −2^(N−1) | 1.2 |
| sign bit | the top bit; 1 ⇒ negative (falls out of two's complement) | 1.2 |
| range asymmetry | `−2^(N−1)…2^(N−1)−1`: one extra negative; `−INT_MIN` overflows | 1.2 |
| `~x + 1 == −x` | the negation identity | 1.2 |
| `std::numeric_limits<T>` | `min/max/digits/is_signed` for a type | 1.2 |
| wraparound (unsigned) | result mod 2^N; **defined** | 1.3 |
| overflow (signed) | result out of range; **undefined behaviour** | 1.3 |
| `-fwrapv` / `-ftrapv` | make signed overflow wrap / trap | 1.3 |
| UBSan | `-fsanitize=undefined`: reports overflow/shift UB at run time | 1.3 |
| checked arithmetic | pre-check (`b>MAX-a`) or `__builtin_*_overflow`; refuse, don't wrap | 1.3 |
| safe midpoint | `left + (right-left)/2` (avoids `left+right` overflow) | 1.3 |
| integral promotion | small types → `int` before arithmetic | 1.4 |
| usual arithmetic conversions | pick a common type; signed→unsigned when unsigned is ≥ rank | 1.4 |
| signed/unsigned trap | `-1 < 1u` is **false** (−1 becomes 2³²−1) | 1.4 |
| `std::cmp_less` etc. | C++20 sign-correct integer comparisons | 1.4 |
| narrowing | storing into a narrower type keeps value mod 2^N | 1.4 |
| `size()-1` bug | empty container → 0−1 wraps huge → OOB | 1.4 |
| endianness | byte order of a multi-byte value (little on x86-64) | 1.5 |
| `std::endian` | query native byte order (C++20) | 1.5 |
| shift codec | portable encode/decode via masks+shifts (no aliasing) | 1.5 |
| `std::byteswap` | reverse an integer's bytes (C++23) | 1.5 |
| `std::bit_cast` | reinterpret bytes as another same-size type, no UB (C++20) | 1.5 |
| integer-overflow→OOB chain | wrapped size → small alloc → full-size write → corruption | 1.6 |
