#include "mm/counting_new.hpp"

#include <cstdlib>
#include <new>

// Exercise 8 — implement the two CORE global operators so every allocation in the program is COUNTED.
// The rest of the family (new[], delete[], sized, nothrow) is in counting_new_family.cpp and delegates
// to these two. Read include/mm/counting_new.hpp (the Header + counters) and Lesson 8.4 first.
//
// NOTE — why these stubs are NOT Todo() throwers: a global operator new that threw would abort the
// program during start-up (the C++ runtime allocates through it before main, Lesson 8.4 §3). So these
// stubs are FUNCTIONAL passthroughs: the program runs, but the counting tests FAIL until you add the
// bookkeeping. Goal: make ./run.sh print ALL TESTS PASSED. Keep new and delete CONSISTENT — if new
// returns raw+kHeader, delete must free raw (= user - kHeader), or you get an invalid free.
//
// Implement:
//   operator new(n):    raw = malloc(kHeader + n); write Header{size=n, magic=kMagic} at raw;
//                       update counters (total_allocations, live_allocations, total_bytes, live_bytes,
//                       peak_bytes); return mm::detail::user_from_raw(raw).  Throw bad_alloc if malloc
//                       fails (Lesson 8.2 §4).
//   operator delete(p): if (!p) return;  h = mm::detail::header_from_user(p);  update counters
//                       (total_frees, live_allocations--, live_bytes -= h->size);  std::free(h).

void* operator new(std::size_t n) {
    // TODO: replace this uncounted passthrough with the header + counting version described above.
    void* p = std::malloc(n == 0 ? 1 : n);
    if (!p) throw std::bad_alloc{};
    return p;
}

void operator delete(void* p) noexcept {
    // TODO: recover the size header, count the free, then free the ORIGINAL pointer.
    std::free(p);
}

// PROVIDED: the sized non-array delete, kept in THIS translation unit so it pairs with the unsized
// one above (avoids -Wsized-deallocation). It just forwards to your core operator delete.
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
