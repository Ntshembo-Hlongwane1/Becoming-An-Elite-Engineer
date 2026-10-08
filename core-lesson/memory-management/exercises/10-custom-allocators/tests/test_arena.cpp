// Exercise 10 — Arena (bump) allocator. Lesson 10.2.
#include <cstdint>

#include "mm/arena.hpp"
#include "minitest.hpp"

using namespace mm;
static std::uintptr_t A(void* p) { return reinterpret_cast<std::uintptr_t>(p); }

TEST(arena_returns_aligned_nonoverlapping_pointers) {
    Arena a(4096);
    void* p = a.allocate(10, 16);
    void* q = a.allocate(10, 16);
    CHECK(p != nullptr); CHECK(q != nullptr);
    CHECK_EQ(A(p) % 16, std::uintptr_t{0});
    CHECK_EQ(A(q) % 16, std::uintptr_t{0});
    CHECK(A(q) >= A(p) + 10);                 // second allocation starts after the first
    CHECK(a.owns(p)); CHECK(a.owns(q));
}

TEST(arena_honours_each_requested_alignment) {
    Arena a(4096);
    (void)a.allocate(1, 1);                   // nudge the offset to something odd
    for (std::size_t al : {std::size_t{1}, std::size_t{8}, std::size_t{16}, std::size_t{64}}) {
        void* p = a.allocate(24, al);
        CHECK(p != nullptr);
        CHECK_EQ(A(p) % al, std::uintptr_t{0});
    }
}

TEST(arena_bumps_used_and_remaining) {
    Arena a(1000);
    std::size_t u0 = a.used();
    a.allocate(100, 1);
    CHECK(a.used() >= u0 + 100);
    CHECK_EQ(a.used() + a.remaining(), a.capacity());
}

TEST(arena_returns_nullptr_when_full) {
    Arena a(64);
    CHECK(a.allocate(40, 1) != nullptr);
    CHECK(a.allocate(40, 1) == nullptr);      // 40 + 40 > 64
    CHECK(a.allocate(1, 1) != nullptr);        // but a small one still fits
}

TEST(arena_reset_reuses_from_the_start) {
    Arena a(4096);
    void* first = a.allocate(32, 16);
    a.allocate(32, 16);
    a.allocate(32, 16);
    a.reset();
    CHECK_EQ(a.used(), std::size_t{0});
    void* again = a.allocate(32, 16);
    CHECK(again == first);                     // same address handed out after reset (Lesson 10.2 §2)
}
