// Exercise 2 — ByteCursor. Lesson 2 (bounds, lifetime, aliasing) + Lesson 1 codec.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <vector>

#include "mm/byte_cursor.hpp"
#include "mm/codec.hpp"
#include "minitest.hpp"

using namespace mm;
using B = std::byte;
static B bt(int v) { return std::byte(static_cast<unsigned char>(v)); }

TEST(reader_scalars_little_endian) {
    std::array<B, 15> buf{bt(0x01), bt(0x02),                        // u8 0x01, then...
                          bt(0xEF), bt(0xBE),                        // u16 LE 0xBEEF
                          bt(0x04), bt(0x03), bt(0x02), bt(0x01),    // u32 LE 0x01020304
                          bt(0x08), bt(0x07), bt(0x06), bt(0x05), bt(0x04), bt(0x03), bt(0x02)};
    ByteReader r{std::span<const B>(buf.data(), buf.size())};
    CHECK(r.read_u8()    == std::optional<std::uint8_t>{0x01});
    CHECK_EQ(r.position(), std::size_t{1});
    CHECK(r.read_u8()    == std::optional<std::uint8_t>{0x02});
    CHECK(r.read_u16_le()== std::optional<std::uint16_t>{0xBEEF});
    CHECK_EQ(r.position(), std::size_t{4});
    CHECK(r.read_u32_le()== std::optional<std::uint32_t>{0x01020304u});
    CHECK_EQ(r.position(), std::size_t{8});
    CHECK_EQ(r.remaining(), std::size_t{7});
    CHECK(!r.read_u64_le().has_value());     // only 7 bytes left; must fail and NOT advance
    CHECK_EQ(r.position(), std::size_t{8});
}

TEST(reader_bytes_and_eof) {
    std::array<B, 4> buf{bt(10), bt(20), bt(30), bt(40)};
    ByteReader r{std::span<const B>(buf.data(), buf.size())};
    auto s = r.read_bytes(3);
    CHECK(s.has_value());
    CHECK_EQ(s->size(), std::size_t{3});
    CHECK(s->data() == buf.data());          // a view INTO the buffer, no copy (Lesson 2.4 §4)
    CHECK_EQ(std::to_integer<int>((*s)[2]), 30);
    CHECK_EQ(r.remaining(), std::size_t{1});
    CHECK(!r.read_bytes(2).has_value());     // only 1 left
    CHECK_EQ(r.position(), std::size_t{3});  // unchanged after failure
    auto s2 = r.read_bytes(1);
    CHECK(s2.has_value());
    CHECK(r.eof());
    auto s3 = r.read_bytes(0);               // zero-length read at eof is valid
    CHECK(s3.has_value());
    CHECK_EQ(s3->size(), std::size_t{0});
}

TEST(reader_rejects_huge_n_without_wrapping) {
    std::array<B, 4> buf{bt(1), bt(2), bt(3), bt(4)};
    ByteReader r{std::span<const B>(buf.data(), buf.size())};
    r.read_u8();                              // position = 1, remaining = 3
    // A wrapping check `position()+n > size()` would compute 1 + SIZE_MAX-ish and wrap. Must reject.
    CHECK(!r.read_bytes(std::numeric_limits<std::size_t>::max()).has_value());
    CHECK(!r.read_bytes(std::numeric_limits<std::size_t>::max() - 1).has_value());
    CHECK_EQ(r.position(), std::size_t{1});   // never advanced
}

TEST(reader_length_prefixed) {
    std::vector<B> buf;
    std::uint8_t hdr[4]; put_u32_le(hdr, 3);
    for (int i = 0; i < 4; ++i) buf.push_back(bt(hdr[i]));
    buf.push_back(bt('a')); buf.push_back(bt('b')); buf.push_back(bt('c'));
    buf.push_back(bt('X'));                    // trailing, must be left alone
    ByteReader r{std::span<const B>(buf.data(), buf.size())};
    auto p = r.read_length_prefixed();
    CHECK(p.has_value());
    CHECK_EQ(p->size(), std::size_t{3});
    CHECK_EQ(std::to_integer<int>((*p)[0]), 'a');
    CHECK_EQ(r.remaining(), std::size_t{1});   // the 'X'
    // truncated: claims 3 but only 2 present after header
    std::vector<B> bad{bt(3), bt(0), bt(0), bt(0), bt('a'), bt('b')};
    ByteReader r2{std::span<const B>(bad.data(), bad.size())};
    CHECK(!r2.read_length_prefixed().has_value());
    CHECK_EQ(r2.position(), std::size_t{0});   // atomic failure: header not consumed either
}

TEST(reader_object_trivially_copyable_and_aliasing_safe) {
    struct Point { std::int32_t x; std::int32_t y; };   // trivially copyable
    static_assert(std::is_trivially_copyable_v<Point>);
    Point p{-7, 123456};
    std::array<B, sizeof(Point)> buf;
    std::memcpy(buf.data(), &p, sizeof p);
    ByteReader r{std::span<const B>(buf.data(), buf.size())};
    auto got = r.read_object<Point>();          // memcpy out; ASan/UBSan clean, no reinterpret_cast
    CHECK(got.has_value());
    CHECK_EQ(got->x, -7);
    CHECK_EQ(got->y, 123456);
    CHECK(r.eof());
    ByteReader r2{std::span<const B>(buf.data(), 3)};   // too small for a Point
    CHECK(!r2.read_object<Point>().has_value());
}

TEST(writer_roundtrip) {
    std::array<B, 64> buf{};
    ByteWriter w{std::span<B>(buf.data(), buf.size())};
    CHECK(w.write_u8(0x7F));
    CHECK(w.write_u16_le(0xBEEF));
    CHECK(w.write_u32_le(0xDEADBEEFu));
    CHECK(w.write_u64_le(0x0102030405060708ull));
    std::array<B, 3> payload{bt('x'), bt('y'), bt('z')};
    CHECK(w.write_length_prefixed(std::span<const B>(payload.data(), payload.size())));
    std::size_t end = w.position();

    ByteReader r{std::span<const B>(buf.data(), end)};
    CHECK(r.read_u8()     == std::optional<std::uint8_t>{0x7F});
    CHECK(r.read_u16_le() == std::optional<std::uint16_t>{0xBEEF});
    CHECK(r.read_u32_le() == std::optional<std::uint32_t>{0xDEADBEEFu});
    CHECK(r.read_u64_le() == std::optional<std::uint64_t>{0x0102030405060708ull});
    auto p = r.read_length_prefixed();
    CHECK(p.has_value() && p->size() == 3);
    CHECK_EQ(std::to_integer<int>((*p)[1]), 'y');
    CHECK(r.eof());
}

TEST(writer_respects_capacity) {
    std::array<B, 4> buf{};
    ByteWriter w{std::span<B>(buf.data(), buf.size())};
    CHECK(w.write_u32_le(0x11223344u));
    CHECK_EQ(w.remaining(), std::size_t{0});
    CHECK(!w.write_u8(0x55));                   // full: must fail, not overflow (ASan guards this)
    CHECK_EQ(w.position(), std::size_t{4});
    // partial item that doesn't fit must not write a truncated value
    std::array<B, 2> small{};
    ByteWriter w2{std::span<B>(small.data(), small.size())};
    CHECK(!w2.write_u32_le(0xAABBCCDDu));
    CHECK_EQ(w2.position(), std::size_t{0});    // nothing written
    CHECK_EQ(std::to_integer<int>(small[0]), 0);
}

TEST(writer_length_prefixed_is_all_or_nothing) {
    // Room for the 4-byte length but NOT the payload: must fail atomically — write nothing, don't
    // advance. A naive impl that writes the length first then fails the payload leaves a corrupt
    // record and a moved cursor.
    std::array<B, 6> buf{};                      // 4 (len) + at most 2 payload
    ByteWriter w{std::span<B>(buf.data(), buf.size())};
    std::array<B, 5> payload{bt(1), bt(2), bt(3), bt(4), bt(5)};   // needs 4+5 = 9 > 6
    CHECK(!w.write_length_prefixed(std::span<const B>(payload.data(), payload.size())));
    CHECK_EQ(w.position(), std::size_t{0});      // nothing written, cursor not advanced
    for (auto x : buf) CHECK_EQ(std::to_integer<int>(x), 0);
}

TEST(writer_object_roundtrip) {
    struct Rec { std::uint16_t a; std::uint8_t b; std::int64_t c; };
    static_assert(std::is_trivially_copyable_v<Rec>);
    Rec in{0xBEEF, 0x42, -1000000};
    std::array<B, 64> buf{};
    ByteWriter w{std::span<B>(buf.data(), buf.size())};
    CHECK(w.write_object(in));
    ByteReader r{std::span<const B>(buf.data(), w.position())};
    auto out = r.read_object<Rec>();
    CHECK(out.has_value());
    CHECK_EQ(out->a, in.a); CHECK_EQ(out->b, in.b); CHECK_EQ(out->c, in.c);
}
