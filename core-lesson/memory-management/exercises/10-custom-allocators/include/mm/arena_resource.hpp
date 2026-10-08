#pragma once
// Exercise 10, part 3 — expose the Arena to the standard library as a std::pmr::memory_resource.
// Notes: Lesson 10.4. Implement the three private virtual overrides in src/arena_resource.cpp; then a
// std::pmr::vector built with `&res` will draw its storage from the arena.
#include <memory_resource>

#include "mm/arena.hpp"

namespace mm {

// A stateful memory_resource backed by one Arena (Lesson 10.4 §2). Because it is stateful, only the
// SAME resource object can free its blocks (do_is_equal compares identity).
class ArenaResource : public std::pmr::memory_resource {
  public:
    explicit ArenaResource(Arena& a) : arena_(a) {}
    Arena& arena() const { return arena_; }

  private:
    // ===== YOU IMPLEMENT (src/arena_resource.cpp) =====

    // Allocate `bytes` aligned to `alignment` from the arena; throw std::bad_alloc if it can't.
    // (Lesson 10.4: do_allocate must return storage aligned to `alignment` or throw.)
    void* do_allocate(std::size_t bytes, std::size_t alignment) override;

    // The arena frees nothing per-object (Lesson 10.2 §2): this is a no-op.
    void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override;

    // Two resources are equal iff memory from one can be freed by the other. For a stateful arena
    // that means they are the SAME object (Lesson 10.4 §2).
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override;

    Arena& arena_;
};

}  // namespace mm
