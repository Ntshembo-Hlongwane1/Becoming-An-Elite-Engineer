#pragma once
// Exercise 8 — an allocation-counting global operator new. Notes: Lesson 8 (esp. 8.4).
//
// You replace the GLOBAL operator new / operator delete family so every `new` and `delete` in the
// whole program is counted. This header provides the counters and a size-header helper; you write
// the operators in src/counting_new.cpp.
//
// WHY A HEADER IS NEEDED (Lesson 8.4 §2): operator delete(void* p) is handed only a pointer, not a
// size, so to count live BYTES you must remember each allocation's size yourself. We store it in a
// 16-byte Header placed just before the pointer we return — exactly like Lesson 7's chunk header.
//
//     raw (from malloc, 16-aligned)        user pointer we return (raw + 16, still 16-aligned)
//       │                                    │
//       ▼                                    ▼
//       ┌───────────────┬────────────────────────────────────────┐
//       │ Header{size,  │  the bytes the caller asked for (size)  │
//       │        magic} │  (object / array lives here)            │
//       └───────────────┴────────────────────────────────────────┘
//
// COUNTERS ARE constinit (Lesson 8.4 §3): operator new is called during static initialisation,
// BEFORE main and before any normal object could be constructed, so the counters must be ready with
// no dynamic initialisation of their own. std::atomic<size_t> has a constexpr constructor, so a
// constinit global of atomics is guaranteed zero-initialised before the first allocation.

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace mm {

struct NewCounters {
    std::atomic<std::size_t> live_allocations{0};   // currently outstanding (new - delete)
    std::atomic<std::size_t> total_allocations{0};  // ever allocated
    std::atomic<std::size_t> total_frees{0};        // ever freed
    std::atomic<std::size_t> live_bytes{0};         // sum of sizes currently outstanding
    std::atomic<std::size_t> total_bytes{0};        // sum of sizes ever requested
    std::atomic<std::size_t> peak_bytes{0};         // high-water mark of live_bytes
};

// THE global counter block. constinit => constant-initialised, ready before main (Lesson 8.4 §3).
inline constinit NewCounters g_new_counters{};

inline NewCounters& new_counters() noexcept { return g_new_counters; }

// Convenience readers (relaxed is fine; tests are single-threaded and read deltas).
inline std::size_t live_allocations() noexcept { return g_new_counters.live_allocations.load(std::memory_order_relaxed); }
inline std::size_t total_allocations() noexcept { return g_new_counters.total_allocations.load(std::memory_order_relaxed); }
inline std::size_t total_frees() noexcept { return g_new_counters.total_frees.load(std::memory_order_relaxed); }
inline std::size_t live_bytes() noexcept { return g_new_counters.live_bytes.load(std::memory_order_relaxed); }
inline std::size_t total_bytes() noexcept { return g_new_counters.total_bytes.load(std::memory_order_relaxed); }
inline std::size_t peak_bytes() noexcept { return g_new_counters.peak_bytes.load(std::memory_order_relaxed); }

// Reset only the running TOTALS and the peak; leave live_* alone (there are always live allocations
// from the C++ runtime, and zeroing them would underflow when those are later freed). Lesson 8.4 §3.
inline void reset_new_totals() noexcept {
    auto& c = g_new_counters;
    c.total_allocations.store(0, std::memory_order_relaxed);
    c.total_frees.store(0, std::memory_order_relaxed);
    c.total_bytes.store(0, std::memory_order_relaxed);
    c.peak_bytes.store(c.live_bytes.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

namespace detail {

// The size header stored before every returned pointer. 16 bytes keeps the user pointer 16-aligned
// (malloc is 16-aligned + 16-byte header => still 16-aligned), matching default new alignment.
struct Header {
    std::size_t size;   // bytes the caller requested (what operator new was given)
    std::size_t magic;  // sanity tag so delete can assert the pointer came from us
};
inline constexpr std::size_t kHeader = 16;                 // == sizeof(Header)
inline constexpr std::size_t kMagic = 0x6D6D4E45574E4557ULL;  // "mmNEWNEW"

static_assert(sizeof(Header) == kHeader, "Header must be 16 bytes to preserve 16-byte alignment");

// Given the raw malloc pointer, where the user pointer starts, and vice versa.
inline void* user_from_raw(void* raw) noexcept { return static_cast<std::byte*>(raw) + kHeader; }
inline Header* header_from_user(void* user) noexcept {
    return reinterpret_cast<Header*>(static_cast<std::byte*>(user) - kHeader);
}

}  // namespace detail
}  // namespace mm
