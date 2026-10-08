#pragma once
// Exercise 1a — checked integer arithmetic. Notes: Lesson 1 §1.3, §1.4.
//
// Rules:
//  * These compute on std::size_t (unsigned, 64-bit here). Unsigned wrap is DEFINED (§1.3), but a
//    wrapped size is still a bug, so these report it instead of returning a wrong value.
//  * checked_* return std::nullopt when the true mathematical result is not representable in
//    std::size_t. They must NOT first form the wrapping result and look at it — detect BEFORE you
//    operate (§1.3 §5a). You may cross-check with __builtin_*_overflow, but a pure-standard
//    pre-check is required for at least one of them (say which in DECISIONS.md).
//  * midpoint has a precondition lo <= hi and must be overflow-free for ALL int inputs, including
//    opposite-sign extremes like (INT_MIN, INT_MAX) where even (hi - lo) overflows int. The simple
//    lo + (hi-lo)/2 form (§1.3 §4a) is NOT enough there; compute the difference as unsigned
//    (hi - lo done in unsigned is the exact non-negative gap, §1.2), halve it, add back. Result
//    rounds toward lo, matching std::midpoint. Explain why the unsigned gap is exact in DECISIONS.md.
//  * less_su returns the MATHEMATICALLY correct value of (a < b), where a is signed and b is
//    unsigned 64-bit. Do NOT use std::cmp_less — implement the reasoning yourself (§1.4 §3).
#include <cstddef>
#include <cstdint>
#include <optional>

namespace mm {

std::optional<std::size_t> checked_add(std::size_t a, std::size_t b);
std::optional<std::size_t> checked_sub(std::size_t a, std::size_t b);  // nullopt if a < b (underflow)
std::optional<std::size_t> checked_mul(std::size_t a, std::size_t b);

int midpoint(int lo, int hi);  // precondition: lo <= hi

bool less_su(std::int64_t a, std::uint64_t b);

}  // namespace mm
