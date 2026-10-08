#pragma once
// Exercise 4 (intermediate+) — AlignedAllocator<T, Align>: a complete standard Allocator so that
//     std::vector<char, am::AlignedAllocator<char, 4096>>
// keeps data() aligned through every reallocation. Notes: Part 4 §4–§5, Part 3 §5, §9.4.
//
// It's a template, so everything lives in this header. Your tasks (marked TODO below):
//  1. `alignment`: never weaker than alignof(T) (Part 4 §4 (2)).
//  2. static_asserts: Align is a power of two; alignment fits in std::align_val_t.
//  3. allocate(n): n OBJECTS, not bytes.
//       - if n * sizeof(T) would overflow -> throw std::bad_array_new_length (what std::allocator does;
//         the book's CustomAllocator skips this — Part 3 §9.4)
//       - ::operator new(bytes, std::align_val_t{alignment})   (Part 3 §5a)
//  4. deallocate(p, n): the MATCHING aligned operator delete (Part 3 §8 case 2). Use the sized form.
//  5. operator==: when can memory from one instance be freed by another? (Part 4 §4 (8))
//
// Provided (study them — Part 4 §5 explains why each is required):
//  * value_type, the converting constructor, and `rebind`. Delete `rebind` once, compile the tests,
//    read the error, put it back, and write in DECISIONS.md what [CPPREF-allocator_traits] says.
#include <cstddef>
#include <limits>
#include <new>

#include "am/todo.hpp"

namespace am {

template <class T, std::size_t Align>
class AlignedAllocator {
public:
    using value_type = T;

    // TODO(Ex4): never weaker than alignof(T). (Placeholder value is deliberately wrong.)
    static constexpr std::size_t alignment = Align;

    // TODO(Ex4): static_assert that Align is a power of two (and non-zero).

    template <class U>
    struct rebind {
        using other = AlignedAllocator<U, Align>;
    };

    AlignedAllocator() noexcept = default;
    template <class U>
    AlignedAllocator(const AlignedAllocator<U, Align>&) noexcept {}

    T* allocate(std::size_t n) {
        (void)n;
        Todo("AlignedAllocator::allocate");
    }

    void deallocate(T* p, std::size_t n) noexcept {
        // TODO(Ex4): matching aligned, sized operator delete.
        // (A no-op in the stub: allocate() never succeeds yet, so there's nothing to free, and a
        //  noexcept function must not call Todo() — it would std::terminate.)
        (void)p;
        (void)n;
    }

    template <class U>
    bool operator==(const AlignedAllocator<U, Align>&) const noexcept {
        // TODO(Ex4): decide and justify (Part 4 §4 (8), Drill 4).
        return false;
    }
};

}  // namespace am
