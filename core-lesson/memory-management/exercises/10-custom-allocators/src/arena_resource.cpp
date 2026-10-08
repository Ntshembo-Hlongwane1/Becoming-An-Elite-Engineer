#include "mm/arena_resource.hpp"
#include "mm/todo.hpp"
#include <new>
// Exercise 10, part 3 — implement the three memory_resource overrides. Notes: Lesson 10.4.
namespace mm {

void* ArenaResource::do_allocate(std::size_t /*bytes*/, std::size_t /*alignment*/) {
    // TODO(Lesson 10.4): allocate from arena_; throw std::bad_alloc if it returns nullptr.
    Todo("ArenaResource::do_allocate");
}

void ArenaResource::do_deallocate(void* /*p*/, std::size_t /*bytes*/, std::size_t /*alignment*/) {
    // TODO(Lesson 10.2 §2): a monotonic arena frees nothing per-object — leave this a no-op.
    Todo("ArenaResource::do_deallocate");
}

bool ArenaResource::do_is_equal(const std::pmr::memory_resource& /*other*/) const noexcept {
    // TODO(Lesson 10.4 §2): a stateful resource equals only itself (identity).
    return false;  // replace: this == &other
}

}  // namespace mm
