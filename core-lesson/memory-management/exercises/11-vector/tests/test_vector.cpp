// Exercise 11 — Vector<T>: basics, rule of five, growth. Lesson 11.
#include <string>
#include <type_traits>

#include "mm/vector.hpp"
#include "minitest.hpp"

using namespace mm;

// counts live instances, to prove RAII frees exactly what it built (plus ASan backs this up)
struct Counted {
    static int live;
    int v;
    Counted(int x = 0) : v(x) { ++live; }
    Counted(const Counted& o) : v(o.v) { ++live; }
    Counted(Counted&& o) noexcept : v(o.v) { ++live; }
    Counted& operator=(const Counted&) = default;
    Counted& operator=(Counted&&) noexcept = default;
    ~Counted() { --live; }
};
int Counted::live = 0;

TEST(default_is_empty) {
    Vector<int> v;
    CHECK_EQ(v.size(), std::size_t{0});
    CHECK(v.empty());
    CHECK_EQ(v.capacity(), std::size_t{0});
}

TEST(push_back_grows_and_indexes) {
    Vector<int> v;
    for (int i = 0; i < 10; ++i) v.push_back(i * i);
    CHECK_EQ(v.size(), std::size_t{10});
    CHECK(v.capacity() >= v.size());
    for (int i = 0; i < 10; ++i) CHECK_EQ(v[static_cast<std::size_t>(i)], i * i);
}

TEST(emplace_back_constructs_in_place_and_returns_ref) {
    Vector<std::string> v;
    std::string& r = v.emplace_back(5, 'x');   // std::string(5,'x') == "xxxxx"
    CHECK_EQ(v.size(), std::size_t{1});
    CHECK_EQ(v[0], std::string("xxxxx"));
    CHECK(&r == &v[0]);
}

TEST(at_throws_out_of_range) {
    Vector<int> v;
    v.push_back(1);
    CHECK_EQ(v.at(0), 1);
    CHECK_THROWS(v.at(1), std::out_of_range);
    CHECK_THROWS(v.at(99), std::out_of_range);
}

TEST(reserve_grows_keeps_elements_and_ignores_shrink) {
    Vector<int> v;
    for (int i = 0; i < 5; ++i) v.push_back(i);
    std::size_t cap0 = v.capacity();
    v.reserve(100);
    CHECK(v.capacity() >= 100);
    CHECK_EQ(v.size(), std::size_t{5});
    for (int i = 0; i < 5; ++i) CHECK_EQ(v[static_cast<std::size_t>(i)], i);  // preserved
    std::size_t cap1 = v.capacity();
    v.reserve(1);                               // smaller -> no-op
    CHECK_EQ(v.capacity(), cap1);
    (void)cap0;
}

TEST(values_survive_many_reallocations) {
    Vector<int> v;
    for (int i = 0; i < 1000; ++i) v.push_back(i);
    CHECK_EQ(v.size(), std::size_t{1000});
    for (int i = 0; i < 1000; ++i) CHECK_EQ(v[static_cast<std::size_t>(i)], i);
}

TEST(copy_ctor_is_a_deep_independent_copy) {
    Vector<int> a;
    for (int i = 0; i < 5; ++i) a.push_back(i);
    Vector<int> b = a;                          // copy ctor
    CHECK_EQ(b.size(), a.size());
    CHECK(b.data() != a.data());                // separate storage
    b[0] = 999;                                 // mutate the copy...
    CHECK_EQ(a[0], 0);                           // ...original unaffected (deep copy)
    for (int i = 1; i < 5; ++i) CHECK_EQ(b[static_cast<std::size_t>(i)], i);
}

TEST(copy_assign_is_deep_and_self_safe) {
    Vector<int> a, b;
    for (int i = 0; i < 4; ++i) a.push_back(i + 10);
    for (int i = 0; i < 9; ++i) b.push_back(-i);
    b = a;                                      // copy-and-swap
    CHECK_EQ(b.size(), std::size_t{4});
    CHECK(b.data() != a.data());
    for (int i = 0; i < 4; ++i) CHECK_EQ(b[static_cast<std::size_t>(i)], i + 10);
    a = a;                                      // self-assignment must be a safe no-op
    CHECK_EQ(a.size(), std::size_t{4});
    CHECK_EQ(a[0], 10);
}

TEST(move_ctor_steals_and_empties_source) {
    Vector<int> a;
    for (int i = 0; i < 6; ++i) a.push_back(i);
    int* buf = a.data();
    Vector<int> b = std::move(a);               // move ctor
    CHECK_EQ(b.size(), std::size_t{6});
    CHECK(b.data() == buf);                     // stole the exact buffer (no copy)
    CHECK_EQ(a.size(), std::size_t{0});          // source left empty & valid
    CHECK_EQ(a.capacity(), std::size_t{0});
    for (int i = 0; i < 6; ++i) CHECK_EQ(b[static_cast<std::size_t>(i)], i);
}

TEST(move_assign_steals_releases_and_is_self_safe) {
    Vector<int> a, b;
    for (int i = 0; i < 3; ++i) a.push_back(i);
    for (int i = 0; i < 7; ++i) b.push_back(i);
    int* buf = a.data();
    b = std::move(a);                           // b's old 7 elements released; steals a's 3
    CHECK_EQ(b.size(), std::size_t{3});
    CHECK(b.data() == buf);
    CHECK_EQ(a.size(), std::size_t{0});
    b = std::move(b);                           // self-move must not corrupt/free-then-use
    CHECK_EQ(b.size(), std::size_t{3});
}

TEST(raii_frees_exactly_what_it_built) {
    CHECK_EQ(Counted::live, 0);
    {
        Vector<Counted> v;
        for (int i = 0; i < 50; ++i) v.emplace_back(i);   // 50 live (growth moves, net live == 50)
        CHECK_EQ(Counted::live, 50);
        Vector<Counted> c = v;                            // +50 (deep copy)
        CHECK_EQ(Counted::live, 100);
    }
    CHECK_EQ(Counted::live, 0);                            // all destroyed — no leak (RAII)
}

TEST(vector_is_nothrow_movable) {
    static_assert(std::is_nothrow_move_constructible_v<Vector<int>>, "move ctor must be noexcept");
    static_assert(std::is_nothrow_move_assignable_v<Vector<int>>, "move assign must be noexcept");
    CHECK(true);
}
