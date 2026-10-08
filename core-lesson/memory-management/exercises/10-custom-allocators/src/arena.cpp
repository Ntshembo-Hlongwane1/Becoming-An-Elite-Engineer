#include "mm/arena.hpp"
#include "mm/todo.hpp"
// Exercise 10, part 1 — implement the Arena. Notes: Lesson 10.2. Read include/mm/arena.hpp first.
namespace mm {

void* Arena::allocate(std::size_t /*n*/, std::size_t /*align*/) {
    // TODO(Lesson 10.2 §1): round the ABSOLUTE address (base_ + off_) up to `align`, fail with
    // nullptr if it won't fit in cap_, else bump off_ and return the aligned pointer.
    Todo("Arena::allocate");
}

void Arena::reset() {
    // TODO(Lesson 10.2 §2): reclaim everything — set the offset back to 0. (No destructors.)
    Todo("Arena::reset");
}

}  // namespace mm
