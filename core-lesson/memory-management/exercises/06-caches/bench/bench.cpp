// Exercise 6 — timing harness (observed, not asserted). Reproduce the Lesson-6 effects on YOUR
// machine and put the numbers in RESULTS.md. Build in Release:
//   cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release && cmake --build build-rel --target bench
//   ./build-rel/bench
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

#include "mm/cache.hpp"
using namespace mm;
using Clock = std::chrono::steady_clock;
static double ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

int main() {
    std::printf("cache line = %zu bytes\n", cache_line_size());

    // 1) row-major vs column-major traversal (Lesson 6.2 §2)
    Matrix m(2048, 2048);
    for (auto& v : m.data) v = 1;
    auto t0 = Clock::now(); volatile std::int64_t s1 = sum_all(m, true);  auto t1 = Clock::now();
    volatile std::int64_t s2 = sum_all(m, false); auto t2 = Clock::now();
    std::printf("sum row-major: %.1f ms   col-major: %.1f ms   ratio %.1fx\n",
                ms(t0,t1), ms(t1,t2), ms(t1,t2) / ms(t0,t1)); (void)s1; (void)s2;

    // 2) naive vs blocked transpose (Lesson 6.3 §1), sweeping tile size
    Matrix a(2048, 2048);
    for (std::size_t i = 0; i < a.data.size(); ++i) a.data[i] = static_cast<std::int64_t>(i);
    auto n0 = Clock::now(); Matrix tn = transpose_naive(a); auto n1 = Clock::now();
    std::printf("transpose naive: %.1f ms\n", ms(n0, n1));
    for (std::size_t b : {8u, 16u, 32u, 64u, 128u, 256u}) {
        auto b0 = Clock::now(); Matrix tb = transpose_blocked(a, b); auto b1 = Clock::now();
        std::printf("transpose blocked(%3zu): %.1f ms  %s\n", b, ms(b0, b1),
                    (tb == tn ? "ok" : "MISMATCH"));
    }

    // 3) false sharing (Lesson 6.4 §2)
    auto hammer = [](std::atomic<long>& x) {
        for (long i = 0; i < 20'000'000; ++i) x.fetch_add(1, std::memory_order_relaxed);
    };
    {
        struct { std::atomic<long> a{0}, b{0}; } together;
        auto s = Clock::now();
        std::thread A(hammer, std::ref(together.a)), B(hammer, std::ref(together.b));
        A.join(); B.join();
        std::printf("false sharing (same line): %.1f ms\n", ms(s, Clock::now()));
    }
    {
        struct { alignas(64) std::atomic<long> a{0}; alignas(64) std::atomic<long> b{0}; } padded;
        auto s = Clock::now();
        std::thread A(hammer, std::ref(padded.a)), B(hammer, std::ref(padded.b));
        A.join(); B.join();
        std::printf("padded (separate lines):   %.1f ms\n", ms(s, Clock::now()));
    }
    return 0;
}
