// Exercise 15 — bounded lock-free MPMC queue. Lesson 15. Built under ThreadSanitizer.
#include <atomic>
#include <thread>
#include <vector>

#include "mm/mpmc_queue.hpp"
#include "minitest.hpp"

using namespace mm;

TEST(single_thread_fifo) {
    MpmcQueue<int, 8> q;
    for (int i = 0; i < 8; ++i) CHECK(q.push(i));   // bounded array of Cap -> Cap usable
    CHECK(!q.push(99));                             // full
    int v;
    for (int i = 0; i < 8; ++i) { CHECK(q.pop(v)); CHECK_EQ(v, i); }   // FIFO
    CHECK(!q.pop(v));                               // empty
}

TEST(wraparound_single_thread) {
    MpmcQueue<int, 4> q;
    int v;
    for (int round = 0; round < 1000; ++round) {
        CHECK(q.push(round)); CHECK(q.push(round + 1));
        CHECK(q.pop(v)); CHECK_EQ(v, round);
        CHECK(q.pop(v)); CHECK_EQ(v, round + 1);
    }
}

// The real test: MANY producers and MANY consumers. Correct (every item received exactly once) AND
// race-free (ThreadSanitizer must report nothing).
TEST(mpmc_transfers_all_items_race_free) {
    constexpr int P = 4, C = 4, PER = 25000;
    constexpr int TOTAL = P * PER;
    MpmcQueue<int, 1024> q;
    std::atomic<long long> sum{0};
    std::atomic<int> consumed{0};

    std::vector<std::thread> ts;
    for (int p = 0; p < P; ++p) ts.emplace_back([&, p] {
        for (int i = 0; i < PER; ++i) {
            int val = p * PER + i;                     // each value unique across all producers
            while (!q.push(val)) std::this_thread::yield();
        }
    });
    for (int c = 0; c < C; ++c) ts.emplace_back([&] {
        int v;
        while (consumed.load(std::memory_order_relaxed) < TOTAL) {
            if (q.pop(v)) { sum.fetch_add(v, std::memory_order_relaxed); consumed.fetch_add(1, std::memory_order_relaxed); }
            else std::this_thread::yield();
        }
    });
    for (auto& t : ts) t.join();

    long long expected = static_cast<long long>(TOTAL) * (TOTAL - 1) / 2;
    CHECK_EQ(consumed.load(), TOTAL);
    CHECK_EQ(sum.load(), expected);                 // every unique value received exactly once
}

TEST(mpmc_single_producer_single_consumer_still_works) {
    constexpr int M = 100000;
    MpmcQueue<int, 256> q;
    long long sum = 0;
    std::thread prod([&] { for (int i = 0; i < M; ++i) while (!q.push(i)) std::this_thread::yield(); });
    std::thread cons([&] { int got = 0, v; while (got < M) { if (q.pop(v)) { sum += v; ++got; } else std::this_thread::yield(); } });
    prod.join(); cons.join();
    CHECK_EQ(sum, static_cast<long long>(M) * (M - 1) / 2);
}
