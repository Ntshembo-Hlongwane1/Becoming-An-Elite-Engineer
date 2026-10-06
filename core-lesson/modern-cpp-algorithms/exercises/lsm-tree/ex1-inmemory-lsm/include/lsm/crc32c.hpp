#pragma once
// M1 — CRC-32C (Castagnoli). Notes: Part 1 §10, Part 3 §8, Part 6 §3 and §6.
//
// Reflected CRC with polynomial 0x82F63B78, initial value 0xFFFFFFFF, final XOR 0xFFFFFFFF.
// Value("123456789") == 0xE3069283. Build a 256-entry table once (constexpr is a nice touch).
// Extend(Value(a), b) must equal Value(a + b) — that's how a CRC covering "type byte + payload"
// is computed without concatenating buffers.
#include <cstddef>
#include <cstdint>

namespace lsm::crc32c {

std::uint32_t Extend(std::uint32_t init_crc, const char* data, std::size_t n);
inline std::uint32_t Value(const char* data, std::size_t n) { return Extend(0, data, n); }

// Masking (LevelDB util/crc32c.h): stored CRCs are rotated and offset so that a CRC of data that
// itself contains CRCs doesn't look valid. Mask(crc) = rotr15(crc) + 0xa282ead8.
std::uint32_t Mask(std::uint32_t crc);
std::uint32_t Unmask(std::uint32_t masked_crc);

}  // namespace lsm::crc32c
