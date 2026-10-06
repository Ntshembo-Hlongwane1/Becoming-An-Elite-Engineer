// M3 — Skip list. Notes Part 3 §4, Part 2 §9, §13.
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <random>
#include <set>
#include <thread>
#include <vector>

#include "lsm/arena.hpp"
#include "lsm/skiplist.hpp"
#include "minitest.hpp"

using namespace lsm;

namespace {
struct U64Cmp {
    int operator()(const std::uint64_t& a, const std::uint64_t& b) const { return a < b ? -1 : (a > b ? 1 : 0); }
};
using List = SkipList<std::uint64_t, U64Cmp>;
}  // namespace

TEST(skiplist_empty) {
    Arena arena;
    List list(U64Cmp{}, &arena);
    CHECK(!list.Contains(10));
    List::Iterator it(&list);
    CHECK(!it.Valid());
    it.SeekToFirst();
    CHECK(!it.Valid());
    it.Seek(100);
    CHECK(!it.Valid());
    it.SeekToLast();
    CHECK(!it.Valid());
}

TEST(skiplist_matches_std_set) {
    Arena arena;
    List list(U64Cmp{}, &arena);
    std::set<std::uint64_t> oracle;
    std::mt19937_64 rng(1234);
    for (int i = 0; i < 20000; ++i) {
        std::uint64_t k = rng() % 1000000;
        if (oracle.insert(k).second) list.Insert(k);
    }
    for (int i = 0; i < 2000; ++i) {
        std::uint64_t k = rng() % 1000000;
        CHECK_EQ(list.Contains(k), oracle.count(k) == 1);
    }
    // Forward iteration
    List::Iterator it(&list);
    it.SeekToFirst();
    for (auto k : oracle) {
        CHECK(it.Valid());
        CHECK_EQ(it.key(), k);
        it.Next();
    }
    CHECK(!it.Valid());
    // Seek == lower_bound
    for (int i = 0; i < 2000; ++i) {
        std::uint64_t t = rng() % 1100000;
        it.Seek(t);
        auto lb = oracle.lower_bound(t);
        if (lb == oracle.end()) {
            CHECK(!it.Valid());
        } else {
            CHECK(it.Valid());
            CHECK_EQ(it.key(), *lb);
        }
    }
    // Backward iteration
    it.SeekToLast();
    for (auto rit = oracle.rbegin(); rit != oracle.rend(); ++rit) {
        CHECK(it.Valid());
        CHECK_EQ(it.key(), *rit);
        it.Prev();
    }
    CHECK(!it.Valid());
}

// One writer, four lock-free readers. Build with -DLSM_TSAN=ON -DCMAKE_BUILD_TYPE=Debug and make
// this TSan-clean. Readers verify: strictly increasing order, and every key they see was fully
// inserted (the writer sets inserted[k] BEFORE Insert(k), so a reader must never see k with
// inserted[k] == false — that would mean it observed an unpublished node).
TEST(skiplist_concurrent_readers) {
    constexpr std::uint64_t N = 20000;
    Arena arena;
    List list(U64Cmp{}, &arena);
    std::vector<std::atomic<bool>> inserted(N);
    for (auto& b : inserted) b.store(false);
    std::atomic<bool> done{false};
    std::atomic<int> violations{0};

    std::vector<std::uint64_t> order(N);
    for (std::uint64_t i = 0; i < N; ++i) order[i] = i;
    std::shuffle(order.begin(), order.end(), std::mt19937_64(99));

    auto reader = [&] {
        while (!done.load(std::memory_order_acquire)) {
            List::Iterator it(&list);
            std::uint64_t prev = 0;
            bool first = true;
            for (it.SeekToFirst(); it.Valid(); it.Next()) {
                std::uint64_t k = it.key();
                if (k >= N || !inserted[k].load(std::memory_order_acquire)) violations++;
                if (!first && k <= prev) violations++;
                prev = k;
                first = false;
            }
        }
    };
    std::vector<std::thread> readers;
    for (int i = 0; i < 4; ++i) readers.emplace_back(reader);
    for (auto k : order) {
        inserted[k].store(true, std::memory_order_release);
        list.Insert(k);
    }
    done.store(true, std::memory_order_release);
    for (auto& t : readers) t.join();
    CHECK_EQ(violations.load(), 0);
    List::Iterator it(&list);
    std::uint64_t count = 0;
    for (it.SeekToFirst(); it.Valid(); it.Next()) ++count;
    CHECK_EQ(count, N);
}
