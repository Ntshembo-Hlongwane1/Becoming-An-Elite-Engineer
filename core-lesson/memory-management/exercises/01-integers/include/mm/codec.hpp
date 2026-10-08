#pragma once
// Exercise 1b — fixed-width integer serialization. Notes: Lesson 1 §1.5.
//
// Rules:
//  * Little-endian = least-significant byte at the lowest address; big-endian = most-significant
//    first (§1.5 §1).
//  * Implement with MASKS and SHIFTS only. No reinterpret_cast of the buffer to an integer pointer
//    (aliasing + alignment UB, §1.5 §2), no memcpy, no std::bit_cast, no std::byteswap. The whole
//    point is that these are portable regardless of the machine's native endianness.
//  * On decode, widen each byte to the result type BEFORE shifting (§1.5 §2). For u64 this is not
//    optional: a byte promotes to int (32-bit), and `int(s[7]) << 56` shifts past the type width —
//    UB (UBSan: "shift exponent 56 too large for int"), and an int accumulator also drops the top
//    32 bits. (For u32 a missing widen happens to be fine under C++20, but widen anyway.)
//  * put_* write exactly the type's size bytes starting at dst. The caller guarantees dst has room.
#include <cstdint>

namespace mm {

void put_u16_le(std::uint8_t* dst, std::uint16_t v);
void put_u16_be(std::uint8_t* dst, std::uint16_t v);
void put_u32_le(std::uint8_t* dst, std::uint32_t v);
void put_u32_be(std::uint8_t* dst, std::uint32_t v);
void put_u64_le(std::uint8_t* dst, std::uint64_t v);
void put_u64_be(std::uint8_t* dst, std::uint64_t v);

std::uint16_t get_u16_le(const std::uint8_t* s);
std::uint16_t get_u16_be(const std::uint8_t* s);
std::uint32_t get_u32_le(const std::uint8_t* s);
std::uint32_t get_u32_be(const std::uint8_t* s);
std::uint64_t get_u64_le(const std::uint8_t* s);
std::uint64_t get_u64_be(const std::uint8_t* s);

}  // namespace mm
