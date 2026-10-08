#pragma once
// Exercise 6 — cache-aware transforms + a timing harness. Notes: Lesson 6.
//
// The TESTS check correctness (a blocked transpose must equal the naive one, etc.) — deterministic,
// pass/fail. The TIMING lives in bench/bench.cpp, which you run to reproduce the 4.8x (row vs col)
// and ~5x (false sharing) effects on your machine for your write-up. Performance is observed, not
// asserted (timings are noisy); correctness is asserted.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mm {

// Row-major dense matrix of int64. element(r,c) == data[r*cols + c].
struct Matrix {
    std::size_t rows = 0, cols = 0;
    std::vector<std::int64_t> data;
    Matrix() = default;
    Matrix(std::size_t r, std::size_t c) : rows(r), cols(c), data(r * c, 0) {}
    std::int64_t& at(std::size_t r, std::size_t c) { return data[r * cols + c]; }
    std::int64_t at(std::size_t r, std::size_t c) const { return data[r * cols + c]; }
    bool operator==(const Matrix&) const = default;
};

// Cache line size in bytes (from sysconf / /sys), a power of two (64 here). Lesson 6.1 §3.
std::size_t cache_line_size();

// Naive transpose: result(c,r) = a(r,c). Simple double loop (reference semantics).
Matrix transpose_naive(const Matrix& a);

// Blocked/tiled transpose (Lesson 6.3 §1): same RESULT as transpose_naive for ANY block>=1 and ANY
// dimensions (including non-square and sizes not divisible by block — handle the ragged edges).
Matrix transpose_blocked(const Matrix& a, std::size_t block);

// Sum all elements. `row_major==true` iterates r outer / c inner (storage order, fast); false
// iterates c outer / r inner (slow). Both MUST return the same sum (Lesson 6.2 §2).
std::int64_t sum_all(const Matrix& a, bool row_major);

}  // namespace mm
