// M1 — encoding & CRC. Notes Part 2 §4–6, Part 3 §8.
#include <limits>
#include <random>

#include "helpers.hpp"
#include "lsm/coding.hpp"
#include "lsm/crc32c.hpp"
#include "minitest.hpp"

using namespace lsm;
using testutil::Hex;

TEST(coding_fixed32_is_little_endian) {
    std::string s;
    PutFixed32(s, 0x01020304u);
    CHECK_EQ(Hex(s), std::string("04 03 02 01"));  // notes Part 2 §4
    CHECK_EQ(DecodeFixed32(s.data()), 0x01020304u);
}

TEST(coding_fixed64_roundtrip_unaligned) {
    std::string s = "x";  // force the value to start at an odd offset
    PutFixed64(s, 0x0102030405060708ull);
    CHECK_EQ(Hex(s.substr(1)), std::string("08 07 06 05 04 03 02 01"));
    CHECK_EQ(DecodeFixed64(s.data() + 1), 0x0102030405060708ull);
}

TEST(coding_varint_known_values) {
    // [PB-ENC] example: 150 -> 96 01. Others measured in notes Part 2 §6.
    struct V { std::uint64_t v; const char* hex; };
    const V cases[] = {{1, "01"}, {127, "7f"}, {128, "80 01"}, {150, "96 01"}, {300, "ac 02"},
                       {16384, "80 80 01"}, {4294967295ull, "ff ff ff ff 0f"}};
    for (const auto& c : cases) {
        std::string s;
        PutVarint64(s, c.v);
        CHECK_EQ(Hex(s), std::string(c.hex));
        CHECK_EQ(static_cast<std::size_t>(VarintLength(c.v)), s.size());
        if (c.v <= 0xffffffffull) {
            std::string t;
            PutVarint32(t, static_cast<std::uint32_t>(c.v));
            CHECK_EQ(t, s);
        }
    }
}

TEST(coding_varint_roundtrip_random) {
    std::mt19937_64 rng(42);
    std::string s;
    std::vector<std::uint64_t> vals;
    for (int i = 0; i < 10000; ++i) {
        std::uint64_t v = rng() >> (rng() % 64);
        vals.push_back(v);
        PutVarint64(s, v);
    }
    std::string_view in(s);
    for (auto v : vals) {
        std::uint64_t got = 0;
        CHECK(GetVarint64(&in, &got));
        CHECK_EQ(got, v);
    }
    CHECK(in.empty());
}

TEST(coding_varint_decoder_rejects_truncated_and_overlong) {
    std::uint32_t v32 = 0;
    std::uint64_t v64 = 0;
    const std::string truncated = "\x80\x80";           // continuation bit set, then nothing
    CHECK(GetVarint32Ptr(truncated.data(), truncated.data() + truncated.size(), &v32) == nullptr);
    const std::string overlong32 = "\x80\x80\x80\x80\x80\x01";  // 6 bytes > 5
    CHECK(GetVarint32Ptr(overlong32.data(), overlong32.data() + overlong32.size(), &v32) == nullptr);
    const std::string overlong64(11, '\x80');
    CHECK(GetVarint64Ptr(overlong64.data(), overlong64.data() + overlong64.size(), &v64) == nullptr);
    const std::string empty;
    CHECK(GetVarint32Ptr(empty.data(), empty.data(), &v32) == nullptr);
    // limit is respected even if more bytes physically follow
    const std::string two = "\x96\x01";
    CHECK(GetVarint32Ptr(two.data(), two.data() + 1, &v32) == nullptr);
    CHECK(GetVarint32Ptr(two.data(), two.data() + 2, &v32) == two.data() + 2);
    CHECK_EQ(v32, 150u);
}

TEST(coding_length_prefixed) {
    std::string s;
    PutLengthPrefixed(s, "hello");
    PutLengthPrefixed(s, std::string_view("\0x\0", 3));
    std::string_view in(s), a, b;
    CHECK(GetLengthPrefixed(&in, &a));
    CHECK(GetLengthPrefixed(&in, &b));
    CHECK_EQ(a, std::string_view("hello"));
    CHECK_EQ(b, std::string_view("\0x\0", 3));
    CHECK(in.empty());
    // A length that claims more bytes than exist must fail (notes Part 7 §2).
    std::string lie;
    PutVarint32(lie, 1000);
    lie += "abc";
    std::string_view lin(lie), r;
    CHECK(!GetLengthPrefixed(&lin, &r));
}

TEST(crc32c_standard_vectors) {
    // RFC 3720 §B.4 (iSCSI) test vectors + the classic "123456789" check value.
    CHECK_EQ(crc32c::Value("123456789", 9), 0xE3069283u);
    std::string zeros(32, '\0'), ones(32, '\xff'), inc(32, '\0');
    for (int i = 0; i < 32; ++i) inc[i] = static_cast<char>(i);
    CHECK_EQ(crc32c::Value(zeros.data(), zeros.size()), 0x8A9136AAu);
    CHECK_EQ(crc32c::Value(ones.data(), ones.size()), 0x62A8AB43u);
    CHECK_EQ(crc32c::Value(inc.data(), inc.size()), 0x46DD794Eu);
}

TEST(crc32c_extend_equals_concatenation) {
    const std::string a = "hello ", b = "world";
    const std::string ab = a + b;
    CHECK_EQ(crc32c::Extend(crc32c::Value(a.data(), a.size()), b.data(), b.size()),
             crc32c::Value(ab.data(), ab.size()));
}

TEST(crc32c_mask) {
    const std::uint32_t crc = crc32c::Value("123456789", 9);
    CHECK(crc32c::Mask(crc) != crc);
    CHECK_EQ(crc32c::Mask(crc), 0xC78AB0E5u);  // rotr15(crc) + 0xa282ead8
    CHECK_EQ(crc32c::Unmask(crc32c::Mask(crc)), crc);
    CHECK(crc32c::Mask(crc32c::Mask(crc)) != crc);
}
