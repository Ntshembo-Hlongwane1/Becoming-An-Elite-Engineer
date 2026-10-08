// Exercise 16 — mini leak detector. Lesson 16.
#include <cstdint>
#include <vector>

#include "mm/leak_tracker.hpp"
#include "minitest.hpp"

using namespace mm;

TEST(tracks_live_allocations_and_bytes) {
    tracker().reset();
    std::vector<void*> ps;
    std::size_t bytes = 0;
    for (int i = 1; i <= 10; ++i) { void* p = tracked_malloc(static_cast<std::size_t>(i * 8)); CHECK(p); ps.push_back(p); bytes += static_cast<std::size_t>(i * 8); }
    CHECK_EQ(tracker().live_count(), std::size_t{10});
    CHECK_EQ(tracker().live_bytes(), bytes);
    CHECK_EQ(tracker().total_allocations(), std::size_t{10});
    for (void* p : ps) tracked_free(p);                 // clean up (keeps ASan happy)
    CHECK_EQ(tracker().live_count(), std::size_t{0});
    CHECK_EQ(tracker().live_bytes(), std::size_t{0});
    CHECK_EQ(tracker().total_allocations(), std::size_t{10});  // total never decreases
}

TEST(freeing_some_leaves_the_rest_outstanding) {
    tracker().reset();
    std::vector<void*> ps;
    for (int i = 0; i < 5; ++i) ps.push_back(tracked_malloc(16));
    tracked_free(ps[1]); tracked_free(ps[3]);           // free 2 of 5
    CHECK_EQ(tracker().live_count(), std::size_t{3});    // 3 still outstanding (the "leaks")
    Leak out[8];
    std::size_t n = tracker().report(out, 8);
    CHECK_EQ(n, std::size_t{3});                         // report lists exactly the 3 live blocks
    for (std::size_t i = 0; i < n; ++i) CHECK_EQ(out[i].size, std::size_t{16});
    // clean up the remaining real blocks
    tracked_free(ps[0]); tracked_free(ps[2]); tracked_free(ps[4]);
    CHECK_EQ(tracker().live_count(), std::size_t{0});
}

TEST(detects_double_free) {
    tracker().reset();
    void* p = tracked_malloc(32);
    CHECK_EQ(tracker().invalid_frees(), std::size_t{0});
    tracked_free(p);                                     // first free: ok
    CHECK_EQ(tracker().live_count(), std::size_t{0});
    tracked_free(p);                                     // second free: double-free, detected, NOT re-freed
    CHECK_EQ(tracker().invalid_frees(), std::size_t{1});
    CHECK_EQ(tracker().live_count(), std::size_t{0});
}

TEST(detects_invalid_free_of_untracked_pointer) {
    tracker().reset();
    int stack_var = 7;
    tracked_free(&stack_var);                            // never tracked -> invalid free, NOT freed
    CHECK_EQ(tracker().invalid_frees(), std::size_t{1});
    CHECK_EQ(tracker().live_count(), std::size_t{0});
    void* p = tracked_malloc(8);                         // real one still works afterwards
    CHECK(p);
    CHECK_EQ(tracker().live_count(), std::size_t{1});
    tracked_free(p);
}

TEST(report_truncates_to_cap_but_returns_true_total) {
    tracker().reset();
    std::vector<void*> ps;
    for (int i = 0; i < 20; ++i) ps.push_back(tracked_malloc(8));
    Leak out[5];
    std::size_t n = tracker().report(out, 5);            // cap smaller than live count
    CHECK_EQ(n, std::size_t{20});                        // returns the TOTAL still live...
    // ...and wrote only the first 5 into out[] (no overflow of out[])
    for (void* p : ps) tracked_free(p);
    CHECK_EQ(tracker().live_count(), std::size_t{0});
}

TEST(reuse_after_free_is_tracked_fresh) {
    tracker().reset();
    void* a = tracked_malloc(64);
    tracked_free(a);                                     // table slot tombstoned
    void* b = tracked_malloc(64);                        // may reuse the same address
    CHECK(b);
    CHECK_EQ(tracker().live_count(), std::size_t{1});     // exactly one live block, whatever the address
    tracked_free(b);
    CHECK_EQ(tracker().live_count(), std::size_t{0});
}
