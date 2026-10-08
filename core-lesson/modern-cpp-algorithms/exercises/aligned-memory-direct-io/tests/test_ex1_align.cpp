// Exercise 1 — alignment arithmetic. Notes Part 1 §4–§9, §14.
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>

#include "am/align.hpp"
#include "minitest.hpp"

using namespace am;
using U = std::uintptr_t;
constexpr std::size_t kMax = std::numeric_limits<std::size_t>::max();
__extension__ typedef unsigned __int128 u128;  // GCC/Clang 128-bit integer, for an overflow-free reference

TEST(ex1_is_power_of_two) {
    CHECK(!IsPowerOfTwo(0));  // the classic edge case (Part 1 §9)
    for (int b = 0; b < 64; ++b) CHECK(IsPowerOfTwo(std::size_t{1} << b));
    for (std::size_t x : {3ul, 5ul, 6ul, 7ul, 12ul, 511ul, 513ul, 4095ul, 3000ul, kMax}) CHECK(!IsPowerOfTwo(x));
    // brute-force agreement with the definition for small numbers
    for (std::size_t x = 1; x < 70000; ++x) {
        int ones = 0;
        for (std::size_t v = x; v; v >>= 1) ones += int(v & 1);
        CHECK_EQ(IsPowerOfTwo(x), ones == 1);
    }
}

TEST(ex1_is_aligned_values) {
    CHECK(IsAligned(U{0}, 512));
    CHECK(IsAligned(U{4096}, 512));
    CHECK(IsAligned(U{0x64d989f1c000}, 4096));   // the aligned_alloc address from Part 1 §14
    CHECK(!IsAligned(U{0x64d989f1a020}, 512));   // your vector's address from Part 1 §14
    CHECK(IsAligned(U{0x64d989f1a020}, 32));
    CHECK(!IsAligned(U{0x64d989f1a020}, 64));
    CHECK(IsAligned(U{12345}, 1));               // everything is 1-aligned
    // invalid alignments are "not aligned", never UB
    CHECK(!IsAligned(U{0}, 0));
    CHECK(!IsAligned(U{3000}, 3000));
    CHECK(!IsAligned(U{6}, 3));
}

TEST(ex1_is_aligned_pointers) {
    alignas(64) static unsigned char buf[256];
    CHECK(IsAligned(static_cast<const void*>(buf), 64));
    CHECK(IsAligned(static_cast<const void*>(buf + 64), 64));
    CHECK(!IsAligned(static_cast<const void*>(buf + 1), 2));
    CHECK(IsAligned(static_cast<const void*>(buf + 8), 8));
    CHECK(!IsAligned(static_cast<const void*>(buf + 8), 16));
}

TEST(ex1_align_down) {
    CHECK_EQ(AlignDown(5000, 4096), std::size_t{4096});
    CHECK_EQ(AlignDown(4096, 4096), std::size_t{4096});
    CHECK_EQ(AlignDown(4095, 4096), std::size_t{0});
    CHECK_EQ(AlignDown(1000, 512), std::size_t{512});
    CHECK_EQ(AlignDown(kMax, 4096), kMax - 4095);
    CHECK_EQ(AlignDown(7, 1), std::size_t{7});
}

TEST(ex1_align_up_basic) {
    CHECK(AlignUp(5000, 4096) == std::optional<std::size_t>{8192});
    CHECK(AlignUp(4096, 4096) == std::optional<std::size_t>{4096});
    CHECK(AlignUp(0, 4096) == std::optional<std::size_t>{0});
    CHECK(AlignUp(1, 4096) == std::optional<std::size_t>{4096});
    CHECK(AlignUp(1000, 512) == std::optional<std::size_t>{1024});
    CHECK(AlignUp(1024, 512) == std::optional<std::size_t>{1024});
    CHECK(AlignUp(13, 1) == std::optional<std::size_t>{13});
}

TEST(ex1_align_up_rejects_bad_alignment) {
    CHECK(!AlignUp(100, 0).has_value());
    CHECK(!AlignUp(100, 3).has_value());
    CHECK(!AlignUp(100, 3000).has_value());
}

TEST(ex1_align_up_overflow_cve_2013_4332) {
    // Part 1 §8: (x + 4095) & ~4095 wraps to 0 for x = SIZE_MAX - 10. Must be refused.
    CHECK(!AlignUp(kMax - 10, 4096).has_value());
    CHECK(!AlignUp(kMax, 2).has_value());
    CHECK(!AlignUp(kMax - 510, 512).has_value());
    // ...but the largest representable aligned values must still work:
    CHECK(AlignUp(kMax - 4095, 4096) == std::optional<std::size_t>{kMax - 4095});
    CHECK(AlignUp(kMax - 4096 - 100, 4096) == std::optional<std::size_t>{kMax - 4095});
    CHECK(AlignUp(kMax, 1) == std::optional<std::size_t>{kMax});
    CHECK(AlignUp(std::size_t{1} << 63, std::size_t{1} << 63) == std::optional<std::size_t>{std::size_t{1} << 63});
    CHECK(!AlignUp((std::size_t{1} << 63) + 1, std::size_t{1} << 63).has_value());
}

TEST(ex1_align_up_matches_division_formula_randomized) {
    std::mt19937_64 rng(2026);
    for (int i = 0; i < 200000; ++i) {
        std::size_t align = std::size_t{1} << (rng() % 20);
        std::size_t x = rng() >> (rng() % 64);  // mix of small and huge values
        auto got = AlignUp(x, align);
        // reference: RocksDB's Roundup ((x + y - 1) / y) * y, done in 128-bit so it can't overflow
        u128 ref = ((static_cast<u128>(x) + align - 1) / align) * align;
        if (ref > kMax) {
            CHECK(!got.has_value());
        } else {
            CHECK(got.has_value());
            CHECK_EQ(*got, static_cast<std::size_t>(ref));
        }
    }
}

TEST(ex1_padding_to) {
    CHECK_EQ(PaddingTo(U{0x1003}, 8), std::size_t{5});    // LevelDB's slop example (notes/lsm-tree Part 2 §2)
    CHECK_EQ(PaddingTo(U{0x1008}, 8), std::size_t{0});
    CHECK_EQ(PaddingTo(U{0x64d989f1a020}, 512), std::size_t{512 - 32});
    CHECK_EQ(PaddingTo(U{1}, 4096), std::size_t{4095});
    CHECK_EQ(PaddingTo(U{12345}, 1), std::size_t{0});
}

TEST(ex1_natural_alignment) {
    CHECK_EQ(NaturalAlignment(U{0x64d989f1a020}), std::size_t{32});   // Part 1 §14
    CHECK_EQ(NaturalAlignment(U{0x64d989f1c000}), std::size_t{0x4000});
    CHECK_EQ(NaturalAlignment(U{1}), std::size_t{1});
    CHECK_EQ(NaturalAlignment(U{12}), std::size_t{4});
    CHECK_EQ(NaturalAlignment(U{std::uintptr_t{1} << 63}), std::size_t{1} << 63);
    CHECK_EQ(NaturalAlignment(U{0}), std::size_t{0});
    std::mt19937_64 rng(7);
    for (int i = 0; i < 100000; ++i) {
        U x = rng() | 1u;              // odd
        int shift = int(rng() % 40);
        U v = x << shift;
        if (v == 0) continue;
        CHECK_EQ(NaturalAlignment(v), std::size_t{1} << shift);
    }
}
