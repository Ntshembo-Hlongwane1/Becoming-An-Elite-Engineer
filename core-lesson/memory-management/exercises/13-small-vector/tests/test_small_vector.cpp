// Exercise 13 — SmallVector<T,N>. Lesson 13.
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "mm/small_vector.hpp"
#include "minitest.hpp"

using namespace mm;

namespace {
template <class SV>
bool data_inside(const SV& sv) {               // does sv.data() point into the sv object itself?
    auto d = reinterpret_cast<std::uintptr_t>(sv.data());
    auto lo = reinterpret_cast<std::uintptr_t>(&sv);
    return d >= lo && d < lo + sizeof(SV);
}
struct Counted { static int live; int v; Counted(int x=0):v(x){++live;} Counted(const Counted&o):v(o.v){++live;}
                 Counted(Counted&&o)noexcept:v(o.v){++live;} ~Counted(){--live;}
                 Counted& operator=(const Counted&)=default; Counted& operator=(Counted&&)noexcept=default; };
int Counted::live = 0;
template <class S> void self_move_assign(S& x) {   // intentional self-move in the test
#if defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wself-move"
#endif
    x = std::move(x);
#if defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif
}
}  // namespace

TEST(fresh_is_inline_empty_capacity_N) {
    SmallVector<int, 4> sv;
    CHECK_EQ(sv.size(), std::size_t{0});
    CHECK_EQ(sv.capacity(), std::size_t{4});
    CHECK(sv.is_inline());
    CHECK(data_inside(sv));                     // storage lives in the object (Lesson 13.2/13.4)
}

TEST(stays_inline_up_to_N_then_spills_to_heap) {
    SmallVector<int, 4> sv;
    for (int i = 0; i < 4; ++i) sv.push_back(i);
    CHECK(sv.is_inline());                       // 4 elements still fit inline
    CHECK(data_inside(sv));
    CHECK_EQ(sv.capacity(), std::size_t{4});
    sv.push_back(99);                            // 5th -> spill to heap
    CHECK(!sv.is_inline());
    CHECK(!data_inside(sv));                      // now points at a heap buffer
    CHECK(sv.capacity() >= 5);
    for (int i = 0; i < 4; ++i) CHECK_EQ(sv[static_cast<std::size_t>(i)], i);  // preserved across spill
    CHECK_EQ(sv[4], 99);
}

TEST(values_survive_many_spills_and_growth) {
    SmallVector<int, 2> sv;
    for (int i = 0; i < 1000; ++i) sv.push_back(i * 3);
    CHECK_EQ(sv.size(), std::size_t{1000});
    CHECK(!sv.is_inline());
    for (int i = 0; i < 1000; ++i) CHECK_EQ(sv[static_cast<std::size_t>(i)], i * 3);
}

TEST(at_throws_out_of_range) {
    SmallVector<int, 4> sv;
    sv.push_back(1);
    CHECK_EQ(sv.at(0), 1);
    CHECK_THROWS(sv.at(1), std::out_of_range);
}

TEST(emplace_back_constructs_in_place) {
    SmallVector<std::string, 2> sv;
    std::string& r = sv.emplace_back(std::size_t{3}, 'z');
    CHECK_EQ(sv[0], std::string("zzz"));
    CHECK(&r == &sv[0]);
}

TEST(move_from_INLINE_source_relocates_into_destination) {
    SmallVector<int, 4> a;
    a.push_back(10); a.push_back(20); a.push_back(30);   // 3 <= 4 -> inline
    CHECK(a.is_inline());
    SmallVector<int, 4> b = std::move(a);
    // CRITICAL: b's storage must be in b's OWN inline buffer, not pointing into the moved-from a.
    CHECK(b.is_inline());
    CHECK(data_inside(b));
    CHECK(b.data() != a.data());                 // not aliasing the dead source
    CHECK_EQ(b.size(), std::size_t{3});
    CHECK_EQ(b[0], 10); CHECK_EQ(b[1], 20); CHECK_EQ(b[2], 30);
    CHECK_EQ(a.size(), std::size_t{0});           // source emptied
}

TEST(move_from_HEAP_source_steals_the_buffer) {
    SmallVector<int, 2> a;
    for (int i = 0; i < 10; ++i) a.push_back(i);  // spilled to heap
    CHECK(!a.is_inline());
    int* heap = a.data();
    SmallVector<int, 2> b = std::move(a);
    CHECK(!b.is_inline());
    CHECK(b.data() == heap);                      // stole the exact heap buffer (no element copy)
    CHECK_EQ(b.size(), std::size_t{10});
    CHECK_EQ(a.size(), std::size_t{0});
    CHECK(a.is_inline());                          // source reset to empty-inline
    for (int i = 0; i < 10; ++i) CHECK_EQ(b[static_cast<std::size_t>(i)], i);
}

TEST(copy_is_deep_and_independent) {
    SmallVector<int, 4> a;
    for (int i = 0; i < 6; ++i) a.push_back(i);    // heap
    SmallVector<int, 4> b = a;
    CHECK_EQ(b.size(), a.size());
    CHECK(b.data() != a.data());
    b[0] = 999;
    CHECK_EQ(a[0], 0);                              // independent
    for (int i = 1; i < 6; ++i) CHECK_EQ(b[static_cast<std::size_t>(i)], i);
}

TEST(copy_assign_is_self_safe) {
    SmallVector<int, 4> a;
    for (int i = 0; i < 3; ++i) a.push_back(i + 1);
    SmallVector<int, 4>& ref = a;
    a = ref;                                       // self copy-assign must be a safe no-op
    CHECK_EQ(a.size(), std::size_t{3});
    CHECK_EQ(a[0], 1); CHECK_EQ(a[2], 3);
}

TEST(move_assign_inline_and_heap) {
    SmallVector<int, 4> a, b;
    a.push_back(1); a.push_back(2);                // inline
    for (int i = 0; i < 9; ++i) b.push_back(i);    // heap
    b = std::move(a);                              // move-assign an INLINE source over a HEAP target
    CHECK(b.is_inline());
    CHECK(data_inside(b));
    CHECK_EQ(b.size(), std::size_t{2});
    CHECK_EQ(b[0], 1); CHECK_EQ(b[1], 2);
    self_move_assign(b);                           // self move-assign
    CHECK_EQ(b.size(), std::size_t{2});
}

TEST(raii_frees_everything_inline_and_heap) {
    Counted::live = 0;
    { SmallVector<Counted, 4> sv; for (int i = 0; i < 3; ++i) sv.emplace_back(i); CHECK_EQ(Counted::live, 3); }
    CHECK_EQ(Counted::live, 0);                    // inline case freed
    { SmallVector<Counted, 4> sv; for (int i = 0; i < 20; ++i) sv.emplace_back(i); CHECK_EQ(Counted::live, 20); }
    CHECK_EQ(Counted::live, 0);                    // heap case freed (ASan also confirms)
}

TEST(holds_move_only_types_across_a_spill) {
    SmallVector<std::unique_ptr<int>, 2> sv;       // move-only; spill must MOVE the unique_ptrs
    for (int i = 0; i < 6; ++i) sv.emplace_back(std::make_unique<int>(i));
    CHECK_EQ(sv.size(), std::size_t{6});
    CHECK(!sv.is_inline());
    for (int i = 0; i < 6; ++i) CHECK_EQ(*sv[static_cast<std::size_t>(i)], i);
    SmallVector<std::unique_ptr<int>, 2> moved = std::move(sv);   // heap move = steal
    CHECK_EQ(moved.size(), std::size_t{6});
    CHECK_EQ(*moved[5], 5);
}
