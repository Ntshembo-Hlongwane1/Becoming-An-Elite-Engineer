// Exercise 2 — AlignedMalloc / AlignedFree on top of malloc. Notes Part 3 §7–§8, Part 7 §1–§2.
// ASan does most of the checking here: writing every byte catches undersized blocks, and freeing
// the wrong pointer is reported as "attempting free on address which was not malloc()-ed".
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <random>
#include <set>
#include <vector>

#include "am/aligned_malloc.hpp"
#include "minitest.hpp"

using namespace am;
constexpr std::size_t kMax = std::numeric_limits<std::size_t>::max();

static bool Aligned(const void* p, std::size_t a) { return reinterpret_cast<std::uintptr_t>(p) % a == 0; }

TEST(ex2_alignment_and_full_size_writable) {
    for (std::size_t align = 1; align <= 65536; align <<= 1) {
        for (std::size_t size : {1ul, 7ul, 8ul, 100ul, 511ul, 512ul, 4096ul, 5000ul}) {
            auto* p = static_cast<unsigned char*>(AlignedMalloc(size, align));
            CHECK(p != nullptr);
            CHECK(Aligned(p, align));
            std::memset(p, 0xAB, size);          // every byte must be ours (ASan)
            CHECK_EQ(p[size - 1], 0xAB);
            AlignedFree(p);
        }
    }
}

TEST(ex2_beats_malloc_luck) {
    // Part 2 §4: plain malloc is only 512-aligned ~3% of the time. Yours must be 100%.
    std::vector<void*> ptrs;
    for (int i = 0; i < 2000; ++i) {
        void* p = AlignedMalloc(4096, 512);
        CHECK(p != nullptr);
        CHECK(Aligned(p, 512));
        ptrs.push_back(p);
    }
    for (void* p : ptrs) AlignedFree(p);
}

TEST(ex2_blocks_do_not_overlap_and_free_in_random_order) {
    std::mt19937 rng(42);
    struct Blk { unsigned char* p; std::size_t n; unsigned char tag; };
    std::vector<Blk> live;
    for (int i = 0; i < 3000; ++i) {
        std::size_t n = 1 + rng() % 3000;
        std::size_t a = std::size_t{1} << (rng() % 13);
        auto* p = static_cast<unsigned char*>(AlignedMalloc(n, a));
        CHECK(p != nullptr);
        CHECK(Aligned(p, a));
        auto tag = static_cast<unsigned char>(i);
        std::memset(p, tag, n);
        live.push_back({p, n, tag});
        if (rng() % 3 == 0 && !live.empty()) {
            std::size_t k = rng() % live.size();
            for (std::size_t j = 0; j < live[k].n; ++j) CHECK_EQ(live[k].p[j], live[k].tag);  // nobody overwrote it
            AlignedFree(live[k].p);
            live.erase(live.begin() + static_cast<std::ptrdiff_t>(k));
        }
    }
    for (auto& b : live) {
        for (std::size_t j = 0; j < b.n; ++j) CHECK_EQ(b.p[j], b.tag);
        AlignedFree(b.p);
    }
}

TEST(ex2_rejects_non_power_of_two_with_einval) {
    for (std::size_t bad : {0ul, 3ul, 12ul, 3000ul}) {
        errno = 0;
        CHECK(AlignedMalloc(64, bad) == nullptr);
        CHECK_EQ(errno, EINVAL);
    }
}

TEST(ex2_overflow_returns_enomem_cve_2013_4332) {
    // Part 7 §1: a huge size must be refused BEFORE any arithmetic wraps around.
    // (If your code wraps, malloc gets a tiny size and the memset below in a real program would
    //  overflow the heap. Here we only check the return value.)
    // Any correct scheme needs at least (align - 1) extra bytes, so every size within 4094 of
    // SIZE_MAX overflows for align = 4096, whatever your exact bookkeeping is.
    for (std::size_t size : {kMax, kMax - 1, kMax - 8, kMax - 100, kMax - 4094}) {
        errno = 0;
        CHECK(AlignedMalloc(size, 4096) == nullptr);
        CHECK_EQ(errno, ENOMEM);
    }
}

TEST(ex2_size_zero_is_unique_and_freeable) {
    std::set<void*> seen;
    std::vector<void*> ptrs;
    for (int i = 0; i < 100; ++i) {
        void* p = AlignedMalloc(0, 64);
        CHECK(p != nullptr);
        CHECK(Aligned(p, 64));
        CHECK(seen.insert(p).second);   // unique while alive
        ptrs.push_back(p);
    }
    for (void* p : ptrs) AlignedFree(p);
}

TEST(ex2_free_null_is_noop) {
    AlignedFree(nullptr);
    void* p = AlignedMalloc(10, 16);    // also proves the stub is gone
    CHECK(p != nullptr);
    AlignedFree(p);
}
