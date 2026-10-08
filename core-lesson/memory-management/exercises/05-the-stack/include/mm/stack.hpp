#pragma once
// Exercise 5 — stack prober + backtrace wrapper. Notes: Lesson 5.
//
//  (a) stack_direction()  — determine grow direction by comparing nested locals (Lesson 5.1 §3).
//  (b) stack_limit_bytes() — RLIMIT_STACK via getrlimit (Lesson 5.1 §4).
//  (c) adjacent_frame_delta() — signed byte distance between an inner and outer frame's local,
//        proving the stack grows down and giving a rough frame stride (Lesson 5.1 §3).
//  (d) capture_backtrace() — wrap glibc backtrace() (Lesson 5.3 §4), the capstone's frame-walk.
#include <cstddef>
#include <cstdint>
#include <span>

namespace mm {

enum class StackDirection { kDown, kUp };

// Compare the address of a local in an outer call with one in an inner call it makes. kDown if the
// inner local has a LOWER address (x86-64). Must actually make a nested call (don't hard-code).
StackDirection stack_direction();

// The soft stack size limit in bytes (getrlimit(RLIMIT_STACK).rlim_cur). If unlimited, return 0.
std::size_t stack_limit_bytes();

// (inner frame local address) - (outer frame local address), in bytes, as a signed value. On a
// down-growing stack this is negative. Use volatile locals / a real nested call so the optimizer
// can't fold it away.
std::ptrdiff_t adjacent_frame_delta();

// Fill `out` with return addresses, innermost first (wrapping backtrace()). Return the count written
// (<= out.size()). The addresses themselves need no symbols; naming is the caller's job.
std::size_t capture_backtrace(std::span<void*> out);

}  // namespace mm
