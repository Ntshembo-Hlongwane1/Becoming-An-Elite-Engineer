// Lesson 11 — reproduce move-vs-copy on reallocation with YOUR Vector<T>. Notes: Lesson 11.4.
// Build Release for timing:  cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release
//                            cmake --build build-rel --target vector_bench && ./build-rel/vector_bench
#include <cstdio>
#include "mm/vector.hpp"

static int g_copies = 0, g_moves = 0;
template <bool NE>
struct Tracked {
    int v = 0;
    Tracked(int x = 0) : v(x) {}
    Tracked(const Tracked& o) : v(o.v) { ++g_copies; }
    Tracked(Tracked&& o) noexcept(NE) : v(o.v) { ++g_moves; }
    Tracked& operator=(const Tracked&) = default;
    Tracked& operator=(Tracked&&) noexcept(NE) = default;
};

template <bool NE>
void demo(const char* label) {
    g_copies = g_moves = 0;
    mm::Vector<Tracked<NE>> v;
    for (int i = 0; i < 1000; ++i) v.emplace_back(i);
    std::printf("%-24s move noexcept=%d  => relocations: copies=%d moves=%d\n",
                label, (int)NE, g_copies, g_moves);
}

int main() {
    demo<true>("noexcept move type:");
    demo<false>("throwing move type:");
    std::puts("(noexcept move => all moves; throwing move => all copies, via move_if_noexcept)");
    return 0;
}
