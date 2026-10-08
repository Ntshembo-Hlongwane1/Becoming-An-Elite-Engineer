// Exercise 1a — checked arithmetic. Lesson 1 §1.3–§1.4.
#include <cstdint>
#include <limits>
#include <optional>
#include <random>

#include "mm/checked.hpp"
#include "minitest.hpp"

using namespace mm;
using S = std::size_t;
constexpr S kMax = std::numeric_limits<S>::max();
using opt = std::optional<S>;

TEST(checked_add_basic) {
    CHECK(checked_add(2, 3) == opt{5});
    CHECK(checked_add(0, 0) == opt{0});
    CHECK(checked_add(kMax, 0) == opt{kMax});
    CHECK(checked_add(kMax - 5, 5) == opt{kMax});
}
TEST(checked_add_overflow) {
    CHECK(!checked_add(kMax, 1).has_value());
    CHECK(!checked_add(kMax, kMax).has_value());
    CHECK(!checked_add(kMax - 4, 5).has_value());
    CHECK(checked_add(kMax - 4, 4) == opt{kMax});   // boundary: exactly fits
}
TEST(checked_sub) {
    CHECK(checked_sub(5, 3) == opt{2});
    CHECK(checked_sub(5, 5) == opt{0});
    CHECK(!checked_sub(3, 5).has_value());   // underflow
    CHECK(!checked_sub(0, 1).has_value());
    CHECK(checked_sub(kMax, kMax) == opt{0});
}
TEST(checked_mul_basic) {
    CHECK(checked_mul(0, kMax) == opt{0});   // 0 * anything = 0, never overflow
    CHECK(checked_mul(kMax, 0) == opt{0});
    CHECK(checked_mul(1, kMax) == opt{kMax});
    CHECK(checked_mul(1000, 1000) == opt{1000000});
    CHECK(checked_mul(1ull << 31, 1ull << 32) == opt{1ull << 63});
}
TEST(checked_mul_overflow) {
    CHECK(!checked_mul(kMax, 2).has_value());
    CHECK(!checked_mul(1ull << 32, 1ull << 32).has_value());
    CHECK(!checked_mul(1'000'000'000, 1'000'000'000'000).has_value());
    // boundary: largest product that still fits
    CHECK(checked_mul(kMax, 1) == opt{kMax});
}
TEST(checked_mul_matches_builtin_randomized) {
    std::mt19937_64 rng(1);
    for (int i = 0; i < 300000; ++i) {
        S a = rng() >> (rng() % 64), b = rng() >> (rng() % 64);
        S out;
        bool of = __builtin_mul_overflow(a, b, &out);
        auto got = checked_mul(a, b);
        if (of) CHECK(!got.has_value());
        else CHECK(got == opt{out});
    }
}

TEST(midpoint_no_overflow) {
    CHECK_EQ(midpoint(0, 10), 5);
    CHECK_EQ(midpoint(0, 9), 4);              // rounds toward lo
    CHECK_EQ(midpoint(4, 4), 4);
    CHECK_EQ(midpoint(-10, 10), 0);
    constexpr int M = std::numeric_limits<int>::max();
    CHECK_EQ(midpoint(M - 1, M), M - 1);
    CHECK_EQ(midpoint(2'000'000'000, 2'000'000'000), 2'000'000'000);   // lo+hi would overflow int
    CHECK_EQ(midpoint(1'500'000'000, 2'000'000'000), 1'750'000'000);
    constexpr int mn = std::numeric_limits<int>::min();
    CHECK_EQ(midpoint(mn, mn + 1), mn);
    // Full opposite-sign range: here even (hi - lo) overflows int, so the naive lo+(hi-lo)/2 is
    // itself UB. The required technique (unsigned difference) stays correct: result is -1.
    CHECK_EQ(midpoint(mn, M), -1);
}
TEST(midpoint_is_in_range_randomized) {
    std::mt19937 rng(2);
    for (int i = 0; i < 200000; ++i) {
        int lo = static_cast<int>(rng()), hi = static_cast<int>(rng());
        if (lo > hi) std::swap(lo, hi);
        int m = midpoint(lo, hi);
        CHECK(lo <= m && m <= hi);
        // reference computed in 64-bit, where nothing overflows: lo + (hi-lo)/2, rounding toward lo.
        long long ref = static_cast<long long>(lo) + (static_cast<long long>(hi) - lo) / 2;
        CHECK_EQ(static_cast<long long>(m), ref);
    }
}

TEST(less_su_signed_unsigned) {
    CHECK_EQ(less_su(-1, 1ull), true);        // the trap: mathematically -1 < 1
    CHECK_EQ(less_su(-1, 0ull), true);
    CHECK_EQ(less_su(0, 0ull), false);
    CHECK_EQ(less_su(5, 5ull), false);
    CHECK_EQ(less_su(5, 6ull), true);
    CHECK_EQ(less_su(-5, 0xFFFFFFFFFFFFFFFFull), true);   // any negative < any unsigned here
    std::int64_t smax = std::numeric_limits<std::int64_t>::max();
    CHECK_EQ(less_su(smax, static_cast<std::uint64_t>(smax)), false);
    CHECK_EQ(less_su(smax, static_cast<std::uint64_t>(smax) + 1), true);   // b exceeds int64 range
    CHECK_EQ(less_su(std::numeric_limits<std::int64_t>::min(), 0ull), true);
}
TEST(less_su_matches_wide_reference_randomized) {
    std::mt19937_64 rng(3);
    for (int i = 0; i < 300000; ++i) {
        auto a = static_cast<std::int64_t>(rng());
        auto b = rng();
        // reference: compare in a type that holds both exactly -> __int128
        __extension__ using i128 = __int128;
        bool ref = (static_cast<i128>(a) < static_cast<i128>(b));
        CHECK_EQ(less_su(a, b), ref);
    }
}
