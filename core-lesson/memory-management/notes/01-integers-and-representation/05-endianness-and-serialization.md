# 1.5 — Endianness and Turning Numbers Into Bytes

A number lives in a register as a value. To put it in memory, a file, or on a wire, it becomes a
sequence of bytes. **Endianness** is the rule for the order of those bytes. Getting this wrong
corrupts every multi-byte field you read or write — and "read bytes from an untrusted source into a
number" is the attack surface of every parser.

## 1. Endianness defined

Take `std::uint32_t v = 0x01020304;`. It has four bytes: `0x01` (most significant) … `0x04` (least
significant). Two layouts exist:

```
little-endian:  address→  04 03 02 01     (least-significant byte at the lowest address)
big-endian:     address→  01 02 03 04     (most-significant byte at the lowest address)
```

**(measured)** on your VM:

```
bytes of 0x01020304 in memory: 04 03 02 01 -> little-endian
std::endian::native is little
```

x86-64 is little-endian. Network protocols (IP, TCP) are big-endian ("network byte order"). So the
moment data crosses a machine or a wire boundary, you must agree on an order. On-disk formats pick
one and stick to it (the LSM lesson's SSTable format fixes little-endian).

Query it portably with `std::endian` from `<bit>` (C++20) `[CPPREF-endian]`:

```cpp
#include <bit>
static_assert(std::endian::native == std::endian::little);   // true on your VM
```

## 2. Looking at a number's bytes — the wrong way and the right way

### Wrong: `reinterpret_cast` and read as another type
```cpp
std::uint32_t v = 0x01020304;
unsigned char* p = reinterpret_cast<unsigned char*>(&v);   // THIS is allowed...
unsigned char b0 = p[0];                                   // ...reading bytes through unsigned char* is OK
```
Reading an object's bytes through `unsigned char*`/`std::byte*` is explicitly permitted (they may
alias anything). But the **value** you get (`0x04` here) is endianness-dependent, so code that does
this to "serialize" produces different bytes on different machines. And going the *other* way —
`int n = *reinterpret_cast<int*>(byte_ptr)` to read an int out of a byte buffer — is usually a
**strict-aliasing and alignment** violation (Lesson 2 §… / alignment lesson Part 1 §12) and is UB.

### Right: shifts (endianness-explicit, portable, no aliasing)
Encode by pulling out each byte with masks and shifts; decode by shifting back in. This produces
the **same bytes on every machine** because you specify the order yourself **(derived)**:

```cpp
// little-endian encode of a 32-bit value into dst[0..3]
void put_u32_le(std::uint8_t* dst, std::uint32_t v) {
    dst[0] = std::uint8_t(v);          // bits  0..7   (least significant first)
    dst[1] = std::uint8_t(v >> 8);     // bits  8..15
    dst[2] = std::uint8_t(v >> 16);    // bits 16..23
    dst[3] = std::uint8_t(v >> 24);    // bits 24..31
}
std::uint32_t get_u32_le(const std::uint8_t* s) {
    return  std::uint32_t(s[0])
         | (std::uint32_t(s[1]) << 8)
         | (std::uint32_t(s[2]) << 16)
         | (std::uint32_t(s[3]) << 24);
}
```

Line notes:
- `std::uint8_t(v)` keeps only the low 8 bits (the conversion is mod 2⁸, §1.4). No mask needed.
- `v >> 8` is a **logical** shift because `v` is unsigned (§3); it brings the next byte down.
  Right-shifting a *signed* value is **arithmetic** (sign-extending, `[STD-expr.shift]`/3), so
  **always serialize through unsigned types.**
- On decode, each `std::uint32_t(s[i])` widens the byte to the result type *before* the shift. Two
  reasons, and getting the reason right teaches you to read the standard by version:
  1. **Width / correctness.** `s[i]` is `std::uint8_t`; in arithmetic it first *promotes to `int`*
     (§1.4 §2), which is only 32-bit. Accumulate a **64-bit** value in an `int`
     (`int r; r |= int(s[i]) << (8*i)`) and the top four bytes are lost.
  2. **Shift-count UB.** `[STD-expr.shift]`/1: shifting by ≥ the width of the promoted left operand
     is UB. In that bad `u64` decode, `int(s[7]) << 56` shifts a 32-bit `int` by 56.
     **(measured)**: UBSan reports `shift exponent 32 is too large for 32-bit type 'int'`. Widening
     to `std::uint64_t` first makes a shift of 56 legal.
- An **outdated** warning you'll meet in older material: pre-C++20, left-shifting a signed value
  into/past the sign bit (e.g. `0xFF << 24` as `int`) was itself UB. **C++20 removed that** —
  `[STD-expr.shift]`/2 defines `E1 << E2` as congruent to `E1×2^E2` modulo `2^N`, so signed left
  shift is now well-defined (wrapping). **(measured)**: under `-std=c++20` a `u32` decode through an
  `int` accumulator is *not* flagged and gives the right bits, because its shift counts stay < 32.
  What remains is reason 2 — which is why `u64` is where a missing widen actually bites. Rule anyway:
  **widen first, every time**; it's correct on every standard version with no case analysis. This is
  why the LSM lesson's `DecodeFixed32` uses shifts with explicit widening, "no pointer casts".

This is LevelDB's `EncodeFixed32`/`DecodeFixed32` approach and is what the LSM exercise's M1 required.

## 3. The shift rules (so decode is never UB)

`[STD-expr.shift]`:
- /1: "The behavior is undefined if the right operand is negative, or greater than or equal to the
  width of the promoted left operand." → never shift a 32-bit value by ≥ 32, nor by a negative.
- /2: `E1 << E2` is `E1 × 2^E2 mod 2^N` for the result type; vacated bits are zero-filled.
- /3: `E1 >> E2` is `E1 / 2^E2` rounded toward −∞; on signed types it is **arithmetic** (sign-
  extending). → right-shift only unsigned values when you want pure bit motion.

## 4. `std::byteswap` and `std::bit_cast` (C++20/23)

Two standard helpers you'll reach for:

- `std::byteswap(x)` (C++23, `<bit>`) reverses the bytes of an integer `[CPPREF-byteswap]` — a
  one-call big↔little conversion when you *know* the stored order differs from native. (Prefer the
  shift codec for portable formats; use `byteswap` when interoperating with a fixed foreign order.)
- `std::bit_cast<To>(from)` (C++20, `<bit>`) reinterprets the bytes of `from` as a `To` of the same
  size, **without** aliasing UB and **without** a memory round-trip you have to write
  `[CPPREF-bit_cast]`. It's the sanctioned replacement for the `reinterpret_cast` in §2 when you
  truly want "same bits, different type" (e.g. float↔uint32 for hashing). It does **not** fix
  endianness — the bytes are taken as-is.

```cpp
std::uint32_t bits = std::bit_cast<std::uint32_t>(3.14f);   // the IEEE-754 bit pattern of 3.14f
```

## 5. Where this lands

Lesson 2's exercise is a `ByteReader`/`ByteWriter` over a raw buffer built on exactly this codec,
plus bounds checks. Every on-disk/on-wire format later (and the LSM format you already built) is
this section applied field by field.

## Drills

1. Implement `put_u32_le`/`get_u32_le` and a `_be` pair. Round-trip 1e6 random values; assert
   `get(put(v)) == v`. Dump the 4 bytes of `0xDEADBEEF` in both orders with `xxd`-style printing.
2. Write the `u64` decode with an `int` accumulator (`int r; r |= int(s[i]) << (8*i)`) and run under
   UBSan; capture the "shift exponent 56 too large for int" report. Then do the `u32` version the
   same way and observe it is *not* flagged under `-std=c++20` (reason 2 vs the removed sign-bit UB).
   Add the widening back.
3. `std::bit_cast` a `float` to `uint32_t` and back; confirm it round-trips. Then `byteswap` the
   uint and show the float changed.
4. Why can't you portably serialize by `fwrite(&v, sizeof v, 1, f)`? (Two reasons: this section and
   the alignment lesson's padding section.)

## My summary
