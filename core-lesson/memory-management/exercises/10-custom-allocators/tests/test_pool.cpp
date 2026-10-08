// Exercise 10 — Pool (fixed-block) allocator. Lesson 10.3.
#include <cstdint>
#include <vector>

#include "mm/pool.hpp"
#include "minitest.hpp"

using namespace mm;
static std::uintptr_t A(void* p) { return reinterpret_cast<std::uintptr_t>(p); }

TEST(pool_starts_with_all_blocks_free) {
    PoolAllocator p(32, 8);
    CHECK_EQ(p.free_count(), std::size_t{8});
    CHECK(p.block_size() >= 32);
    CHECK(p.block_size() >= sizeof(PoolAllocator::FreeNode));   // must hold a free-list node (10.3 §3)
}

TEST(pool_hands_out_distinct_blocks_within_the_buffer) {
    PoolAllocator p(64, 5);
    std::vector<void*> got;
    for (int i = 0; i < 5; ++i) {
        void* b = p.allocate();
        CHECK(b != nullptr);
        CHECK(p.owns(b));                       // inside the buffer, on a block boundary
        CHECK_EQ(A(b) % 16, std::uintptr_t{0});  // default alignment
        got.push_back(b);
    }
    CHECK_EQ(p.free_count(), std::size_t{0});
    for (std::size_t i = 0; i < got.size(); ++i)
        for (std::size_t j = i + 1; j < got.size(); ++j)
            CHECK(got[i] != got[j]);            // all distinct
}

TEST(pool_returns_nullptr_when_empty) {
    PoolAllocator p(16, 2);
    CHECK(p.allocate() != nullptr);
    CHECK(p.allocate() != nullptr);
    CHECK(p.allocate() == nullptr);             // exhausted (Lesson 10.3 §1)
}

TEST(pool_recycles_a_freed_block) {
    PoolAllocator p(32, 4);
    void* a = p.allocate();
    std::size_t free_after_alloc = p.free_count();
    p.deallocate(a);
    CHECK_EQ(p.free_count(), free_after_alloc + 1);
    void* b = p.allocate();
    CHECK(b == a);                              // freed block handed straight back (Lesson 10.3 §4)
}

TEST(pool_deallocate_null_is_a_noop) {
    PoolAllocator p(16, 3);
    std::size_t f = p.free_count();
    p.deallocate(nullptr);
    CHECK_EQ(p.free_count(), f);
}

TEST(pool_full_cycle_allocate_free_reallocate) {
    PoolAllocator p(48, 16);
    std::vector<void*> got;
    for (int i = 0; i < 16; ++i) got.push_back(p.allocate());
    CHECK_EQ(p.free_count(), std::size_t{0});
    for (void* b : got) { CHECK(b != nullptr); p.deallocate(b); }
    CHECK_EQ(p.free_count(), std::size_t{16});  // all back
    for (int i = 0; i < 16; ++i) CHECK(p.allocate() != nullptr);  // and reusable
    CHECK(p.allocate() == nullptr);
}

TEST(pool_blocks_are_usable_storage_no_overlap_when_written) {
    // Write a distinct pattern into every live block; verify none clobbers another (blocks disjoint).
    PoolAllocator p(sizeof(long), 32);
    std::vector<long*> ps;
    for (int i = 0; i < 32; ++i) {
        auto* x = static_cast<long*>(p.allocate());
        CHECK(x != nullptr);
        *x = 0x1122334455667700L + i;
        ps.push_back(x);
    }
    for (int i = 0; i < 32; ++i) CHECK_EQ(*ps[static_cast<std::size_t>(i)], 0x1122334455667700L + i);
    for (long* x : ps) p.deallocate(x);
}
