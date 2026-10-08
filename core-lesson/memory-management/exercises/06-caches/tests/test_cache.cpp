// Exercise 6 — cache-aware transforms (correctness). Lesson 6.
#include <cstdint>
#include <random>

#include "mm/cache.hpp"
#include "minitest.hpp"

using namespace mm;

static Matrix random_matrix(std::size_t r, std::size_t c, unsigned seed) {
    Matrix m(r, c);
    std::mt19937_64 rng(seed);
    for (auto& v : m.data) v = static_cast<std::int64_t>(rng());
    return m;
}

TEST(cache_line_size_is_power_of_two) {
    std::size_t ls = cache_line_size();
    CHECK(ls >= 16);
    CHECK((ls & (ls - 1)) == 0);
}

TEST(transpose_naive_correct) {
    Matrix a(2, 3);
    a.at(0,0)=1; a.at(0,1)=2; a.at(0,2)=3;
    a.at(1,0)=4; a.at(1,1)=5; a.at(1,2)=6;
    Matrix t = transpose_naive(a);
    CHECK_EQ(t.rows, std::size_t{3});
    CHECK_EQ(t.cols, std::size_t{2});
    CHECK_EQ(t.at(0,0), 1); CHECK_EQ(t.at(1,0), 2); CHECK_EQ(t.at(2,0), 3);
    CHECK_EQ(t.at(0,1), 4); CHECK_EQ(t.at(1,1), 5); CHECK_EQ(t.at(2,1), 6);
}

TEST(transpose_blocked_equals_naive_square) {
    for (std::size_t n : {1u, 2u, 7u, 16u, 17u, 64u, 100u}) {
        Matrix a = random_matrix(n, n, static_cast<unsigned>(n));
        Matrix ref = transpose_naive(a);
        for (std::size_t b : {1u, 2u, 8u, 16u, 32u, 64u}) {
            CHECK(transpose_blocked(a, b) == ref);     // same result for every block size
        }
    }
}

TEST(transpose_blocked_equals_naive_rectangular) {
    // non-square, and sizes not divisible by block (ragged edges)
    for (auto [r, c] : {std::pair{3u,5u}, {5u,3u}, {17u,31u}, {64u,1u}, {1u,64u}, {100u,37u}}) {
        Matrix a = random_matrix(r, c, r * 1000 + c);
        Matrix ref = transpose_naive(a);
        for (std::size_t b : {1u, 3u, 8u, 16u, 50u}) {
            Matrix got = transpose_blocked(a, b);
            CHECK_EQ(got.rows, c);
            CHECK_EQ(got.cols, r);
            CHECK(got == ref);
        }
    }
}

TEST(transpose_is_involution) {
    Matrix a = random_matrix(40, 24, 7);
    CHECK(transpose_blocked(transpose_blocked(a, 8), 8) == a);   // (A^T)^T == A
}

TEST(sum_all_order_independent) {
    for (auto [r, c] : {std::pair{10u,10u}, {64u,33u}, {1u,1000u}, {1000u,1u}}) {
        Matrix a = random_matrix(r, c, r + c);
        std::int64_t s_row = sum_all(a, true);
        std::int64_t s_col = sum_all(a, false);
        CHECK_EQ(s_row, s_col);          // identical arithmetic, different traversal order
    }
    Matrix a(3,3);
    for (std::size_t i=0;i<9;++i) a.data[i]=static_cast<std::int64_t>(i+1);   // 1..9 -> 45
    CHECK_EQ(sum_all(a, true), 45);
    CHECK_EQ(sum_all(a, false), 45);
}
