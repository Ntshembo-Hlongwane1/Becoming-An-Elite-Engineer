// Exercise 8 — allocation-counting global operator new. Correctness. Lesson 8.
//
// Each test measures a DELTA around one allocating statement: it reads a counter, does exactly one
// new/delete, and reads again. (Absolute totals are meaningless because the C++ runtime itself
// allocates before main through your replaced operators — Lesson 8.4 §3.) Nothing between the two
// reads may allocate, so the test bodies avoid std::string/std::vector on the measured path.
#include <new>

#include "mm/counting_new.hpp"
#include "minitest.hpp"

TEST(single_new_delete_tracks_live_allocations) {
    auto a0 = mm::live_allocations();
    int* p = new int(42);
    CHECK_EQ(mm::live_allocations() - a0, std::size_t{1});   // one live allocation appeared
    CHECK_EQ(*p, 42);                                        // and the object was constructed
    auto f0 = mm::total_frees();
    delete p;
    CHECK_EQ(mm::live_allocations(), a0);                    // back to baseline
    CHECK_EQ(mm::total_frees() - f0, std::size_t{1});        // one free recorded
}

TEST(bytes_tracked_for_a_known_size) {
    auto b0 = mm::total_bytes();
    auto lb0 = mm::live_bytes();
    double* p = new double;                                  // operator new(sizeof(double)=8)
    CHECK_EQ(mm::total_bytes() - b0, sizeof(double));
    CHECK_EQ(mm::live_bytes() - lb0, sizeof(double));
    delete p;
    CHECK_EQ(mm::live_bytes(), lb0);                         // live bytes return to baseline
}

TEST(total_allocations_counts_every_new) {
    auto t0 = mm::total_allocations();
    int* a = new int;
    int* b = new int;
    int* c = new int;
    CHECK_EQ(mm::total_allocations() - t0, std::size_t{3});
    delete a; delete b; delete c;
}

TEST(array_new_is_one_allocation) {
    auto t0 = mm::total_allocations();
    auto b0 = mm::total_bytes();
    int* arr = new int[8];                                   // ONE operator new[] call (Lesson 8.1 §4)
    CHECK_EQ(mm::total_allocations() - t0, std::size_t{1});
    CHECK(mm::total_bytes() - b0 >= 8 * sizeof(int));        // >= to allow an array cookie
    delete[] arr;
}

TEST(nothrow_new_is_counted_like_normal) {
    auto a0 = mm::live_allocations();
    int* p = new (std::nothrow) int(7);                      // Lesson 8.2 §2
    CHECK(p != nullptr);
    CHECK_EQ(mm::live_allocations() - a0, std::size_t{1});
    CHECK_EQ(*p, 7);
    delete p;
    CHECK_EQ(mm::live_allocations(), a0);
}

TEST(class_with_ctor_dtor_is_one_allocation) {
    struct Widget { int a, b, c; Widget() : a(1), b(2), c(3) {} };
    auto a0 = mm::live_allocations();
    auto b0 = mm::total_bytes();
    Widget* w = new Widget;                                  // new = operator new + constructor (8.1)
    CHECK_EQ(mm::live_allocations() - a0, std::size_t{1});
    CHECK_EQ(mm::total_bytes() - b0, sizeof(Widget));
    CHECK_EQ(w->a + w->b + w->c, 6);
    delete w;
    CHECK_EQ(mm::live_allocations(), a0);
}

TEST(peak_bytes_is_a_high_water_mark) {
    mm::reset_new_totals();                                  // sets peak := current live bytes
    auto live_before = mm::live_bytes();
    char* big = new char[100000];
    auto peak_after = mm::peak_bytes();
    CHECK(peak_after >= live_before + 100000);              // peak rose to include the big block
    delete[] big;
    CHECK(mm::peak_bytes() >= peak_after);                  // and does NOT fall when freed
}

TEST(outstanding_allocations_are_visible_then_cleaned) {
    // 'leak' = outstanding live allocations. We hold 10, observe them, then free (so ASan sees no
    // real leak at exit). Uses the raw operators so there is no constructor/array cookie in the way.
    auto a0 = mm::live_allocations();
    void* blocks[10];
    for (auto& b : blocks) b = ::operator new(64);
    CHECK_EQ(mm::live_allocations() - a0, std::size_t{10});
    for (auto& b : blocks) ::operator delete(b);
    CHECK_EQ(mm::live_allocations(), a0);
}

TEST(delete_nullptr_is_a_noop) {
    auto a0 = mm::live_allocations();
    auto f0 = mm::total_frees();
    int* p = nullptr;
    delete p;                                               // Lesson 8: no-op, must not touch counters
    CHECK_EQ(mm::live_allocations(), a0);
    CHECK_EQ(mm::total_frees(), f0);
}
