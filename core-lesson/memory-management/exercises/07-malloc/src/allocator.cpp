#include "mm/allocator.hpp"

#include "mm/todo.hpp"

// Exercise 7 — implement these 6 functions. Everything you need (layout, navigation, set_block,
// fl_push/fl_remove, next_block/prev_block, block_size_for) is in mm/allocator.hpp — read its header
// comment first. Notes: ../../notes/07-how-malloc-works/. Delete each Todo() as you implement it.
namespace mm {

// Round n up to a multiple of a (a is a power of two). Lesson 7.2 §3.
std::size_t FreeListAllocator::align_up(std::size_t /*n*/, std::size_t /*a*/) {
    Todo("align_up");
}

// First-fit: first free-list block with size >= need, else nullptr. Lesson 7.3 §2.
BlockHeader* FreeListAllocator::find_fit(std::size_t /*need*/) {
    Todo("find_fit");
}

// If the remainder (h->size - need) is >= kMinBlock, carve it off as a free block pushed to the
// list and shrink h to `need`; otherwise leave h unchanged. See the header's precondition. 7.3 §3.
void FreeListAllocator::split(BlockHeader* /*h*/, std::size_t /*need*/) {
    Todo("split");
}

// Merge a just-freed, on-the-list block with any free physical neighbour(s); return the survivor.
// All four cases of Lesson 7.4 §1. Use next_block/prev_block, fl_remove/fl_push, set_block.
BlockHeader* FreeListAllocator::coalesce(BlockHeader* /*h*/) {
    Todo("coalesce");
}

// round up -> find_fit -> remove from list -> split -> mark in use -> return payload. 7.3 §5.
void* FreeListAllocator::allocate(std::size_t /*n*/) {
    Todo("allocate");
}

// find block -> mark free -> push on list -> coalesce. nullptr is a no-op. Lesson 7.4.
void FreeListAllocator::deallocate(void* /*p*/) {
    Todo("deallocate");
}

}  // namespace mm
