// Exercise 14 — lock-free SPSC ring buffer. Lesson 14. Built under ThreadSanitizer.
#include <atomic>
#include <thread>

#include "mm/spsc_ring.hpp"
#include "minitest.hpp"

using namespace mm;

TEST(single_thread_push_pop_fifo) {
    SpscRing<int, 8> q;                       // usable capacity 7
    CHECK(q.empty_approx());
    for (int i = 0; i < 7; ++i) CHECK(q.push(i));
    int v;
    for (int i = 0; i < 7; ++i) { CHECK(q.pop(v)); CHECK_EQ(v, i); }   // FIFO order
    CHECK(q.empty_approx());
}

TEST(reports_full_and_empty) {
    SpscRing<int, 4> q;                       // usable capacity 3
    CHECK(q.push(1)); CHECK(q.push(2)); CHECK(q.push(3));
    CHECK(!q.push(4));                        // full (one slot reserved)
    int v;
    CHECK(q.pop(v)); CHECK_EQ(v, 1);
    CHECK(q.push(4));                         // room again
    CHECK(q.pop(v)); CHECK_EQ(v, 2);
    CHECK(q.pop(v)); CHECK_EQ(v, 3);
    CHECK(q.pop(v)); CHECK_EQ(v, 4);
    CHECK(!q.pop(v));                         // empty
}

TEST(wraparound_many_cycles_single_thread) {
    SpscRing<int, 4> q;
    int v;
    for (int round = 0; round < 1000; ++round) {   // push/pop far past Cap to exercise modulo wrap
        CHECK(q.push(round)); CHECK(q.push(round + 1));
        CHECK(q.pop(v)); CHECK_EQ(v, round);
        CHECK(q.pop(v)); CHECK_EQ(v, round + 1);
    }
    CHECK(q.empty_approx());
}

// The real test: a producer thread and a consumer thread exchange M items. Must be correct AND
// race-free — ThreadSanitizer will fail the run if the orderings don't establish happens-before.
TEST(two_threads_transfer_all_items_race_free) {
    constexpr int M = 200000;
    SpscRing<int, 1024> q;
    long long sum = 0;

    std::thread prod([&] {
        for (int i = 0; i < M; ++i)
            while (!q.push(i)) std::this_thread::yield();
    });
    std::thread cons([&] {
        int got = 0, v;
        while (got < M) {
            if (q.pop(v)) { sum += v; ++got; }
            else std::this_thread::yield();
        }
    });
    prod.join();
    cons.join();

    long long expected = static_cast<long long>(M) * (M - 1) / 2;
    CHECK_EQ(sum, expected);                   // every item received exactly once, with the right value
}
