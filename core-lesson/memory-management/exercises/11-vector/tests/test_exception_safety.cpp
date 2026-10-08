// Exercise 11 — Vector<T>: the strong guarantee + move_if_noexcept + move-only elements. Lesson 11.4.
#include <memory>
#include <stdexcept>

#include "mm/vector.hpp"
#include "minitest.hpp"

using namespace mm;

// relocation counters, to observe move-vs-copy during reallocation (Lesson 11.4 §3)
static int g_copies = 0, g_moves = 0;
template <bool NoexceptMove>
struct Tracked {
    int v = 0;
    Tracked(int x = 0) : v(x) {}
    Tracked(const Tracked& o) : v(o.v) { ++g_copies; }
    Tracked(Tracked&& o) noexcept(NoexceptMove) : v(o.v) { ++g_moves; }
    Tracked& operator=(const Tracked&) = default;
    Tracked& operator=(Tracked&&) noexcept(NoexceptMove) = default;
};

TEST(reallocation_moves_when_move_is_noexcept) {
    g_copies = g_moves = 0;
    Vector<Tracked<true>> v;
    for (int i = 0; i < 8; ++i) v.emplace_back(i);   // several reallocations
    CHECK(g_moves > 0);
    CHECK_EQ(g_copies, 0);                            // noexcept move => never copies on realloc
}

TEST(reallocation_copies_when_move_can_throw) {
    g_copies = g_moves = 0;
    Vector<Tracked<false>> v;
    for (int i = 0; i < 8; ++i) v.emplace_back(i);
    CHECK(g_copies > 0);                              // throwing move => move_if_noexcept picks COPY
    CHECK_EQ(g_moves, 0);
}

// copy throws when armed; move ctor is suppressed (user copy ops) so reserve relocates by COPY,
// letting the throw fire mid-reallocation — the vector must be left exactly as it was.
struct Bomb {
    static bool armed;
    int v;
    Bomb(int x = 0) : v(x) {}
    Bomb(const Bomb& o) : v(o.v) { if (armed) throw std::runtime_error("boom"); }
    Bomb& operator=(const Bomb&) = default;
};
bool Bomb::armed = false;

TEST(reserve_has_the_strong_guarantee) {
    Vector<Bomb> v;
    for (int i = 0; i < 3; ++i) v.push_back(Bomb(i));
    Bomb* buf_before = v.data();
    std::size_t size_before = v.size();

    Bomb::armed = true;
    bool threw = false;
    try {
        v.reserve(100);                               // relocation copies -> first copy throws
    } catch (const std::runtime_error&) {
        threw = true;
    }
    Bomb::armed = false;

    CHECK(threw);
    CHECK_EQ(v.size(), size_before);                  // unchanged
    CHECK(v.data() == buf_before);                    // old buffer kept (rolled back)
    for (int i = 0; i < 3; ++i) CHECK_EQ(v[static_cast<std::size_t>(i)].v, i);   // values intact
}

TEST(push_back_has_the_strong_guarantee) {
    Vector<Bomb> v;
    for (int i = 0; i < 4; ++i) v.push_back(Bomb(i));  // now size==capacity (power-of-two growth)
    while (v.size() < v.capacity()) v.push_back(Bomb(100));  // ensure size==capacity so next realloc
    std::size_t size_before = v.size();

    Bomb::armed = true;
    bool threw = false;
    try { v.push_back(Bomb(7)); } catch (const std::runtime_error&) { threw = true; }
    Bomb::armed = false;

    CHECK(threw);
    CHECK_EQ(v.size(), size_before);                  // the element was NOT added; vector intact
    for (int i = 0; i < 4; ++i) CHECK_EQ(v[static_cast<std::size_t>(i)].v, i);
}

TEST(holds_move_only_types) {
    Vector<std::unique_ptr<int>> v;                   // unique_ptr is move-only (no copy)
    for (int i = 0; i < 5; ++i) v.emplace_back(std::make_unique<int>(i));
    v.push_back(std::make_unique<int>(99));           // push_back(T&&)
    CHECK_EQ(v.size(), std::size_t{6});
    for (int i = 0; i < 5; ++i) CHECK_EQ(*v[static_cast<std::size_t>(i)], i);
    CHECK_EQ(*v[5], 99);

    Vector<std::unique_ptr<int>> w = std::move(v);    // the whole vector moves
    CHECK_EQ(w.size(), std::size_t{6});
    CHECK_EQ(v.size(), std::size_t{0});
    CHECK_EQ(*w[5], 99);
}
