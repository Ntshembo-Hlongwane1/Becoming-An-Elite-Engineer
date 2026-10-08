#pragma once
// Exercise 18 — a minimal coverage-guided fuzzer. Notes: Lesson 18.2.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mm {
// The harness you point at the code under test (you implement it in target.cpp). Must be
// deterministic and stateless across calls (Lesson 18.3 §1).
void fuzz_one(const std::uint8_t* data, std::size_t size);

// Coverage bitmap, shared with the instrumentation callback. One byte per edge-hash (Lesson 18.2 §2).
inline constexpr unsigned kMapSize = 1u << 12;   // power of two
extern std::uint8_t g_covmap[kMapSize];          // per-run: which edges fired (engine resets it)

// YOU IMPLEMENT (engine.cpp): the two functions that make a fuzzer "coverage-guided".
bool interesting();                               // did this run hit an edge never seen before?
void mutate(std::vector<std::uint8_t>& input, std::uint64_t& rng_state);  // perturb an input
}  // namespace mm
