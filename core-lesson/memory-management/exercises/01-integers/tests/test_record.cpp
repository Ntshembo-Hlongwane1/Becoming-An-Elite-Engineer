// Exercise 1c — safe length-prefixed record parser. Lesson 1 §1.5–§1.6.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

#include "mm/codec.hpp"
#include "mm/record.hpp"
#include "minitest.hpp"

using namespace mm;

static std::vector<std::byte> make_record(std::uint32_t len, std::size_t actual_payload) {
    std::vector<std::byte> v(4 + actual_payload);
    std::uint8_t hdr[4];
    put_u32_le(hdr, len);
    std::memcpy(v.data(), hdr, 4);
    for (std::size_t i = 0; i < actual_payload; ++i) v[4 + i] = std::byte(0x40 + (i & 0x3F));
    return v;
}
static std::span<const std::byte> as_span(const std::vector<std::byte>& v) { return {v.data(), v.size()}; }

TEST(record_valid) {
    auto v = make_record(5, 5);
    auto p = parse_record(as_span(v));
    CHECK(p.has_value());
    CHECK_EQ(p->size(), std::size_t{5});
    CHECK(p->data() == v.data() + 4);                 // a view INTO buf, no copy
    CHECK_EQ(std::to_integer<int>((*p)[0]), 0x40);
    CHECK(bytes_consumed(as_span(v)) == std::optional<std::size_t>{9});
}

TEST(record_zero_length_is_valid_empty) {
    auto v = make_record(0, 0);
    auto p = parse_record(as_span(v));
    CHECK(p.has_value());
    CHECK_EQ(p->size(), std::size_t{0});
    CHECK(p->data() != nullptr);                      // non-null, zero-size span into buf
    CHECK(bytes_consumed(as_span(v)) == std::optional<std::size_t>{4});
}

TEST(record_trailing_bytes_ignored) {
    auto v = make_record(3, 100);                      // claims 3, buffer has 100 payload bytes
    auto p = parse_record(as_span(v));
    CHECK(p.has_value());
    CHECK_EQ(p->size(), std::size_t{3});               // only the claimed 3
    CHECK(bytes_consumed(as_span(v)) == std::optional<std::size_t>{7});
}

TEST(record_too_short_for_length_header) {
    for (std::size_t n : {std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{3}}) {
        std::vector<std::byte> v(n, std::byte{0});
        CHECK(!parse_record(as_span(v)).has_value());
        CHECK(!bytes_consumed(as_span(v)).has_value());
    }
}

TEST(record_payload_past_end_rejected) {
    auto v = make_record(10, 5);                       // claims 10 payload, only 5 present
    CHECK(!parse_record(as_span(v)).has_value());
    CHECK(!bytes_consumed(as_span(v)).has_value());
    auto w = make_record(1, 0);                        // claims 1, zero present
    CHECK(!parse_record(as_span(w)).has_value());
}

TEST(record_huge_length_does_not_wrap) {
    // The attack (§1.6): a length near 2^32 must be REJECTED, not wrap when added to 4.
    std::vector<std::byte> v(4 + 8);
    std::uint8_t hdr[4];
    for (std::uint32_t len : {0xFFFFFFFFu, 0xFFFFFFFCu, 0xFFFFFFFBu, 0x80000000u, 0x7FFFFFFFu}) {
        put_u32_le(hdr, len);
        std::memcpy(v.data(), hdr, 4);
        CHECK(!parse_record(as_span(v)).has_value());  // payload can't possibly fit in 12 bytes
        CHECK(!bytes_consumed(as_span(v)).has_value());
    }
}

TEST(record_exact_fit_boundary) {
    auto v = make_record(8, 8);                        // payload exactly fills the buffer
    auto p = parse_record(as_span(v));
    CHECK(p.has_value());
    CHECK_EQ(p->size(), std::size_t{8});
    CHECK(bytes_consumed(as_span(v)) == std::optional<std::size_t>{12});
    // one byte short must fail
    std::vector<std::byte> w(v.begin(), v.end() - 1);
    CHECK(!parse_record(as_span(w)).has_value());
}
