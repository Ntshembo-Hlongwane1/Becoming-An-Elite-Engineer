// M2 — Arena. Notes Part 2 §2, §8.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <random>

#include "lsm/arena.hpp"
#include "minitest.hpp"

using namespace lsm;

TEST(arena_aligned_after_odd_allocations) {
    Arena arena;
    std::mt19937 rng(7);
    for (int i = 0; i < 5000; ++i) {
        std::size_t n = 1 + rng() % 37;
        char* p = arena.Allocate(n);
        std::memset(p, 0xab, n);  // ASan catches out-of-bounds
        char* q = arena.AllocateAligned(1 + rng() % 64);
        CHECK(reinterpret_cast<std::uintptr_t>(q) % alignof(std::max_align_t) == 0);
        std::memset(q, 0xcd, 1);
    }
}

TEST(arena_large_allocations) {
    Arena arena;
    char* small = arena.Allocate(10);
    char* big = arena.Allocate(100000);      // > kBlockSize/4: dedicated block
    std::memset(big, 1, 100000);
    char* small2 = arena.Allocate(10);
    // The big block must not have consumed the current block's remaining space.
    CHECK(small2 == small + 10);
    CHECK(arena.MemoryUsage() >= 100000 + Arena::kBlockSize);
}

TEST(arena_memory_usage_grows) {
    Arena arena;
    std::size_t total = 0;
    for (int i = 0; i < 1000; ++i) {
        std::size_t n = (i % 100) + 1;
        arena.Allocate(n);
        total += n;
        CHECK(arena.MemoryUsage() >= total);
    }
}

TEST(arena_objects_survive_until_destruction) {
    auto arena = std::make_unique<Arena>();
    std::vector<std::uint64_t*> ptrs;
    for (std::uint64_t i = 0; i < 10000; ++i) {
        auto* p = new (arena->AllocateAligned(sizeof(std::uint64_t))) std::uint64_t(i * 31);
        ptrs.push_back(p);
    }
    for (std::uint64_t i = 0; i < ptrs.size(); ++i) CHECK_EQ(*ptrs[i], i * 31);
    arena.reset();  // frees everything at once; ASan reports leaks if the destructor doesn't
}
