#include "mm/leak_tracker.hpp"
#include <cstdlib>
// PROVIDED: the global tracker instance and the allocation hooks (Lesson 16.4 §1).
namespace mm {
LeakTracker& tracker() { static LeakTracker t; return t; }
void* tracked_malloc(std::size_t n) {
    void* p = std::malloc(n);
    if (p) tracker().track(p, n);               // record metadata on allocate
    return p;
}
void tracked_free(void* p) {
    if (p && tracker().untrack(p))              // only free what WE handed out and still own...
        std::free(p);                           // ...so a double-/invalid-free never reaches std::free
}
}  // namespace mm
