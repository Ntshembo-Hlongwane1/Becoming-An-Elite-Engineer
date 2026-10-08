// Exercise 1b — fixed-width codec. Lesson 1 §1.5.
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

#include "mm/codec.hpp"
#include "minitest.hpp"

using namespace mm;

TEST(codec_known_byte_patterns) {
    std::uint8_t b[8];
    put_u32_le(b, 0x01020304u);
    CHECK_EQ(b[0], 0x04); CHECK_EQ(b[1], 0x03); CHECK_EQ(b[2], 0x02); CHECK_EQ(b[3], 0x01);
    put_u32_be(b, 0x01020304u);
    CHECK_EQ(b[0], 0x01); CHECK_EQ(b[1], 0x02); CHECK_EQ(b[2], 0x03); CHECK_EQ(b[3], 0x04);
    put_u16_le(b, 0xBEEFu);
    CHECK_EQ(b[0], 0xEF); CHECK_EQ(b[1], 0xBE);
    put_u16_be(b, 0xBEEFu);
    CHECK_EQ(b[0], 0xBE); CHECK_EQ(b[1], 0xEF);
    put_u64_le(b, 0x0102030405060708ull);
    for (int i = 0; i < 8; ++i) CHECK_EQ(b[i], 8 - i);     // 08 07 06 05 04 03 02 01
    put_u64_be(b, 0x0102030405060708ull);
    for (int i = 0; i < 8; ++i) CHECK_EQ(b[i], i + 1);     // 01 02 ... 08
}

TEST(codec_decode_high_bit_bytes_no_ub) {
    // Bytes >= 0x80 in the top position: the classic "forgot to widen before shift" UB (§1.5 §2).
    // Built under UBSan; a bad implementation reports a shift/overflow error here.
    std::uint8_t b[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    CHECK_EQ(get_u16_le(b), 0xFFFFu);
    CHECK_EQ(get_u32_le(b), 0xFFFFFFFFu);
    CHECK_EQ(get_u64_le(b), 0xFFFFFFFFFFFFFFFFull);
    std::uint8_t c[4] = {0x00, 0x00, 0x00, 0x80};
    CHECK_EQ(get_u32_le(c), 0x80000000u);
    std::uint8_t d[4] = {0x80, 0x00, 0x00, 0x00};
    CHECK_EQ(get_u32_be(d), 0x80000000u);
}

TEST(codec_roundtrip_randomized) {
    std::mt19937_64 rng(10);
    std::uint8_t b[8];
    for (int i = 0; i < 200000; ++i) {
        auto v16 = static_cast<std::uint16_t>(rng());
        put_u16_le(b, v16); CHECK_EQ(get_u16_le(b), v16);
        put_u16_be(b, v16); CHECK_EQ(get_u16_be(b), v16);
        auto v32 = static_cast<std::uint32_t>(rng());
        put_u32_le(b, v32); CHECK_EQ(get_u32_le(b), v32);
        put_u32_be(b, v32); CHECK_EQ(get_u32_be(b), v32);
        std::uint64_t v64 = rng();
        put_u64_le(b, v64); CHECK_EQ(get_u64_le(b), v64);
        put_u64_be(b, v64); CHECK_EQ(get_u64_be(b), v64);
    }
}

TEST(codec_le_be_are_byte_reverses) {
    std::mt19937_64 rng(11);
    std::uint8_t le[8], be[8];
    for (int i = 0; i < 100000; ++i) {
        std::uint64_t v = rng();
        put_u64_le(le, v);
        put_u64_be(be, v);
        for (int k = 0; k < 8; ++k) CHECK_EQ(le[k], be[7 - k]);   // LE is BE reversed
    }
}

TEST(codec_writes_exactly_its_own_bytes) {
    // put_u16 must not touch bytes beyond index 1, etc. Sentinels catch over-writes (also ASan if
    // the buffer were heap). We use a padded stack buffer and check the guard bytes.
    std::uint8_t b[8];
    std::memset(b, 0xAA, sizeof b);
    put_u16_le(b, 0x1234);
    CHECK_EQ(b[2], 0xAA); CHECK_EQ(b[3], 0xAA);
    std::memset(b, 0xAA, sizeof b);
    put_u32_le(b, 0x12345678u);
    CHECK_EQ(b[4], 0xAA);
}
