// Lesson 10 — reproduce the arena/pool/pmr measurements with the EXERCISE's own allocators.
// Build Release for timing:  cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release
//                            cmake --build build-rel --target alloc_bench && ./build-rel/alloc_bench
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory_resource>
#include <vector>

#include "mm/arena.hpp"
#include "mm/arena_resource.hpp"
#include "mm/pool.hpp"

int main() {
    using namespace mm;
    constexpr int N = 1'000'000;
    auto msf = [](auto a, auto b){ return std::chrono::duration<double, std::milli>(b - a).count(); };

    std::vector<void*> ptrs(static_cast<std::size_t>(N));   // allocated before timing
    Arena ar(64ull * 1024 * 1024);                           // allocated before timing...
    for (int i = 0; i < N; ++i) ar.allocate(32, 16);         // ...and first-touched (warm the pages)
    ar.reset();

    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i) ptrs[static_cast<std::size_t>(i)] = std::malloc(32);
    for (int i = 0; i < N; ++i) std::free(ptrs[static_cast<std::size_t>(i)]);
    auto t1 = std::chrono::steady_clock::now();

    for (int i = 0; i < N; ++i) ptrs[static_cast<std::size_t>(i)] = ar.allocate(32, 16);
    ar.reset();
    auto t2 = std::chrono::steady_clock::now();
    std::printf("alloc %d x 32B then release:  malloc+free %.1f ms   arena+reset %.1f ms  (%.1fx)\n",
                N, msf(t0, t1), msf(t1, t2), msf(t0, t1) / msf(t1, t2));

    PoolAllocator pool(32, 4);
    void* a = pool.allocate();
    pool.deallocate(a);
    void* b = pool.allocate();
    std::printf("pool: freed %p -> next alloc %p  (recycled=%s)\n", a, b, a == b ? "yes" : "no");

    Arena pa(1 << 16);
    ArenaResource res(pa);
    std::pmr::vector<int> v{&res};
    for (int i = 0; i < 100; ++i) v.push_back(i);
    std::printf("pmr::vector data()=%p  arena.owns=%s  arena.used=%zu bytes\n",
                (void*)v.data(), pa.owns(v.data()) ? "yes" : "no", pa.used());
    return 0;
}
