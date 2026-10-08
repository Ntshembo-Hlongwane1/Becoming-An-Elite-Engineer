#pragma once
// Exercise 10, part 1 — the Arena (bump / monotonic) allocator. Notes: Lesson 10.2.
//
// Owns ONE buffer and hands out slices by bumping a single offset. O(1) allocate, NO per-object
// free — only reset() reclaims everything at once. You implement allocate() and reset(); the ctor,
// dtor, and introspection are provided.
#include <cstddef>
#include <cstdint>
#include <new>

namespace mm {

class Arena {
  public:
    // Take `capacity` bytes from the system once, aligned to 16 (default new alignment, Lesson 9).
    explicit Arena(std::size_t capacity)
        : base_(static_cast<std::byte*>(::operator new(capacity ? capacity : 1,
                                                        std::align_val_t(16)))),
          cap_(capacity) {}
    ~Arena() { ::operator delete(base_, std::align_val_t(16)); }
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    // ===== YOU IMPLEMENT (src/arena.cpp) =====

    // Return `n` bytes aligned to `align` (a power of two), or nullptr if the buffer can't fit it.
    // Bump the offset forward. Align the ABSOLUTE address (round base_+off_ up to `align`), so any
    // alignment works regardless of base_'s alignment (Lesson 10.2 §1, Lesson 9).
    void* allocate(std::size_t n, std::size_t align);

    // Reclaim the WHOLE buffer at once (offset -> 0). Runs no destructors (Lesson 10.2 §3).
    void reset();

    // ===== PROVIDED =====
    std::size_t used() const { return off_; }
    std::size_t capacity() const { return cap_; }
    std::size_t remaining() const { return cap_ - off_; }
    std::byte* base() const { return base_; }
    bool owns(const void* p) const {
        auto a = reinterpret_cast<std::uintptr_t>(p);
        return a >= reinterpret_cast<std::uintptr_t>(base_) &&
               a < reinterpret_cast<std::uintptr_t>(base_) + cap_;
    }

  private:
    std::byte* base_ = nullptr;
    std::size_t cap_ = 0;
    std::size_t off_ = 0;   // the bump pointer, as an offset from base_
};

}  // namespace mm
