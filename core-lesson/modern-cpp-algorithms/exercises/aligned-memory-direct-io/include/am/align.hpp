#pragma once
// Exercise 1 (beginner) — Alignment arithmetic. Notes: Part 1 §4–§9, §14.
//
// Rules for this exercise:
//  * No `%` and no `/` anywhere in src/align.cpp. Use masks only (Part 1 §7–§8). That's the point.
//  * No <bit> helpers (std::has_single_bit, std::countr_zero, ...) either: write the bit tricks
//    yourself, THEN compare with <bit> in DECISIONS.md.
//  * No function may throw or hit UB: invalid input is reported through the return value. Once a
//    function is implemented, mark it `noexcept` in both files (the stubs can't be noexcept,
//    because Todo() throws and a throw out of a noexcept function calls std::terminate).
//
// Extra credit: make every function `constexpr`, move the bodies into this header, and add a
// block of static_assert()s at the bottom of src/align.cpp that prove your arithmetic at compile time.
#include <cstddef>
#include <cstdint>
#include <optional>

namespace am {

// True iff x has exactly one bit set (1, 2, 4, ...). 0 is NOT a power of two. Part 1 §9.
bool IsPowerOfTwo(std::size_t x);

// True iff `value` is a multiple of `align`. Returns false if `align` is not a power of two.
bool IsAligned(std::uintptr_t value, std::size_t align);

// Same, for the address held by a pointer (Part 1 §4: convert with reinterpret_cast).
bool IsAligned(const void* p, std::size_t align);

// Largest multiple of `align` that is <= x. Precondition: IsPowerOfTwo(align). Part 1 §8.
std::size_t AlignDown(std::size_t x, std::size_t align);

// Smallest multiple of `align` that is >= x.
// Returns std::nullopt if `align` is not a power of two OR if the result is not representable in
// std::size_t (the CVE-2013-4332 bug class — Part 1 §8, Part 7 §1). Detect overflow BEFORE adding.
std::optional<std::size_t> AlignUp(std::size_t x, std::size_t align);

// How many bytes must be added to `addr` to reach the next multiple of `align` (0 if already
// aligned). Precondition: IsPowerOfTwo(align). This is LevelDB's `slop` (Part 1 §7).
std::size_t PaddingTo(std::uintptr_t addr, std::size_t align);

// The largest power of two that divides `addr` (Part 1 §14). By definition returns 0 for addr == 0
// (every power of two divides 0, so there is no largest one).
std::size_t NaturalAlignment(std::uintptr_t addr);

}  // namespace am
