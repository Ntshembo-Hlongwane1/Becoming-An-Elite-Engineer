// Exercise 7 — free-list allocator (split + coalesce). Correctness. Lesson 7.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

#include "mm/allocator.hpp"
#include "minitest.hpp"

using namespace mm;

using Alloc = FreeListAllocator;

static std::uintptr_t addr(void* p) { return reinterpret_cast<std::uintptr_t>(p); }

// Owned extent of an allocation = its chunk's payload capacity (>= requested).
static std::size_t owned(void* p) { return Alloc::payload_capacity(Alloc::header_from_payload(p)); }

TEST(fresh_arena_is_one_free_block) {
    Alloc a(4096);
    CHECK(a.check_invariants());
    CHECK_EQ(a.block_count(), std::size_t{1});
    CHECK_EQ(a.free_block_count(), std::size_t{1});
    // one block covering the whole arena => its payload is total minus the 32 bytes of tags
    CHECK_EQ(a.largest_free_payload(), a.total_bytes() - Alloc::kOverhead);
}

TEST(returned_pointers_are_16_aligned) {
    Alloc a(1 << 16);
    for (std::size_t n : {std::size_t{1}, std::size_t{7}, std::size_t{16}, std::size_t{17}, std::size_t{100}, std::size_t{255}, std::size_t{1000}}) {
        void* p = a.allocate(n);
        CHECK(p != nullptr);
        CHECK_EQ(addr(p) % Alloc::kAlign, std::uintptr_t{0});   // Lesson 7.2 §3
    }
    CHECK(a.check_invariants());
}

TEST(capacity_is_at_least_requested_and_writable) {
    Alloc a(1 << 16);
    for (std::size_t n : {std::size_t{1}, std::size_t{16}, std::size_t{50}, std::size_t{300}}) {
        auto* p = static_cast<unsigned char*>(a.allocate(n));
        CHECK(p != nullptr);
        CHECK(owned(p) >= n);                       // usable_size >= request (Lesson 7.2)
        std::memset(p, 0xAB, owned(p));             // writing the whole payload must stay in-bounds
        for (std::size_t i = 0; i < owned(p); ++i) CHECK_EQ(p[i], 0xAB);
    }
    CHECK(a.check_invariants());
}

TEST(allocations_do_not_overlap_and_stay_in_arena) {
    Alloc a(1 << 16);
    std::vector<void*> ps;
    for (int i = 0; i < 20; ++i) {
        void* p = a.allocate(static_cast<std::size_t>(16 + i * 8));
        CHECK(p != nullptr);
        ps.push_back(p);
    }
    for (void* p : ps) {
        CHECK(addr(p) >= addr(a.base()));
        CHECK(addr(p) + owned(p) <= addr(a.end()));
    }
    for (std::size_t i = 0; i < ps.size(); ++i)
        for (std::size_t j = i + 1; j < ps.size(); ++j) {
            auto ai = addr(ps[i]), aj = addr(ps[j]);
            bool disjoint = ai + owned(ps[i]) <= aj || aj + owned(ps[j]) <= ai;
            CHECK(disjoint);                        // no two live payloads overlap
        }
    CHECK(a.check_invariants());
}

TEST(allocate_splits_an_oversized_block) {
    Alloc a(4096);
    std::size_t before = a.largest_free_payload();
    void* p = a.allocate(16);                       // tiny request out of a big free block
    CHECK(p != nullptr);
    CHECK_EQ(a.block_count(), std::size_t{2});       // split => used block + free remainder
    CHECK_EQ(a.free_block_count(), std::size_t{1});
    // the remainder shrank by exactly the carved block (one min block = 48)
    CHECK_EQ(a.largest_free_payload(), before - Alloc::kMinBlock);
    CHECK(a.check_invariants());
}

TEST(allocate_does_not_split_when_remainder_too_small) {
    // Arena with a single block whose leftover after the request would be < kMinBlock (48):
    // request needs a 48-byte block; make the whole arena 48 + 40 = 88, leftover 40 < 48.
    Alloc a(Alloc::kMinBlock + 40);
    CHECK_EQ(a.block_count(), std::size_t{1});
    void* p = a.allocate(16);                        // need = 48, block = 88, remainder 40 -> no split
    CHECK(p != nullptr);
    CHECK_EQ(a.block_count(), std::size_t{1});        // still one block (whole thing handed over)
    CHECK_EQ(a.free_block_count(), std::size_t{0});
    CHECK(owned(p) >= 16);
    CHECK(a.check_invariants());
}

TEST(out_of_memory_returns_nullptr_and_preserves_heap) {
    Alloc a(256);
    void* big = a.allocate(10000);                   // far larger than the arena
    CHECK(big == nullptr);
    CHECK(a.check_invariants());
    CHECK_EQ(a.block_count(), std::size_t{1});        // untouched
}

TEST(free_null_is_a_noop) {
    Alloc a(1024);
    a.deallocate(nullptr);                            // Lesson 7.1 §5
    CHECK(a.check_invariants());
    CHECK_EQ(a.block_count(), std::size_t{1});
}

TEST(allocate_zero_is_valid_and_freeable) {
    Alloc a(1024);
    void* p = a.allocate(0);                          // Lesson 7.1 §5
    CHECK(p != nullptr);
    CHECK_EQ(addr(p) % Alloc::kAlign, std::uintptr_t{0});
    a.deallocate(p);
    CHECK(a.check_invariants());
}

TEST(coalesce_forward_merges_next_free_neighbour) {
    Alloc a(4096);
    void* p0 = a.allocate(32);
    void* p1 = a.allocate(32);
    void* p2 = a.allocate(32);                        // keep p2 live so the tail stays pinned
    CHECK(p0 && p1 && p2);
    std::size_t blocks = a.block_count();
    a.deallocate(p1);                                 // free the middle; neighbours in use -> no merge
    a.deallocate(p0);                                 // now p0 and (freed) p1 are adjacent & free
    CHECK(a.free_block_count() >= 1);
    CHECK(a.block_count() < blocks);                  // p0+p1 coalesced into one
    CHECK(a.check_invariants());                      // invariant: no two adjacent free blocks
    a.deallocate(p2);
}

TEST(free_everything_coalesces_back_to_one_block) {
    Alloc a(1 << 16);
    std::size_t whole = a.largest_free_payload();
    std::vector<void*> ps;
    for (int i = 0; i < 50; ++i) {
        void* p = a.allocate(static_cast<std::size_t>(24 + (i % 7) * 16));
        CHECK(p != nullptr);
        ps.push_back(p);
    }
    // free in a shuffled order to exercise all four coalesce cases (Lesson 7.4 §1)
    std::mt19937 rng(12345);
    std::shuffle(ps.begin(), ps.end(), rng);
    for (void* p : ps) a.deallocate(p);
    CHECK_EQ(a.block_count(), std::size_t{1});         // everything merged back
    CHECK_EQ(a.free_block_count(), std::size_t{1});
    CHECK_EQ(a.largest_free_payload(), whole);         // fully recovered, no leak, no fragmentation
    CHECK(a.check_invariants());
}

TEST(freed_space_is_reused) {
    Alloc a(4096);
    void* p = a.allocate(64);
    std::size_t blocks_after_alloc = a.block_count();
    a.deallocate(p);
    void* q = a.allocate(64);                          // same size should reuse the just-freed room
    CHECK(q != nullptr);
    CHECK_EQ(a.block_count(), blocks_after_alloc);      // no new block created => reuse, not growth
    CHECK(a.check_invariants());
}

// The real test: a long random mix of allocate/free, verifying after EVERY step that the heap is
// well-formed, live payloads never overlap, and each allocation's bytes are untouched by its
// neighbours (writes a unique fill per allocation and checks it just before freeing).
TEST(randomized_stress_keeps_the_heap_consistent) {
    Alloc a(1 << 18);
    std::mt19937 rng(2026);
    std::uniform_int_distribution<int> sz(1, 2000);
    std::uniform_int_distribution<int> coin(0, 1);
    struct Live { unsigned char* p; std::size_t n; unsigned char fill; };
    std::vector<Live> live;
    unsigned char next_fill = 1;

    for (int step = 0; step < 4000; ++step) {
        bool do_alloc = live.empty() || coin(rng) == 0;
        if (do_alloc) {
            std::size_t n = static_cast<std::size_t>(sz(rng));
            auto* p = static_cast<unsigned char*>(a.allocate(n));
            if (p) {                                   // nullptr just means the arena is full right now
                CHECK(owned(p) >= n);
                unsigned char f = next_fill++; if (next_fill == 0) next_fill = 1;
                std::memset(p, f, n);
                // no overlap with any existing live allocation
                for (auto& L : live) {
                    auto a1 = addr(p), a2 = addr(L.p);
                    bool disjoint = a1 + owned(p) <= a2 || a2 + owned(L.p) <= a1;
                    CHECK(disjoint);
                }
                live.push_back({p, n, f});
            }
        } else {
            std::uniform_int_distribution<std::size_t> pick(0, live.size() - 1);
            std::size_t i = pick(rng);
            Live L = live[i];
            for (std::size_t k = 0; k < L.n; ++k) CHECK_EQ(L.p[k], L.fill);  // unclobbered
            a.deallocate(L.p);
            live[i] = live.back();
            live.pop_back();
        }
        CHECK(a.check_invariants());
    }
    for (auto& L : live) a.deallocate(L.p);
    CHECK_EQ(a.block_count(), std::size_t{1});          // drained -> fully coalesced
    CHECK(a.check_invariants());
}
