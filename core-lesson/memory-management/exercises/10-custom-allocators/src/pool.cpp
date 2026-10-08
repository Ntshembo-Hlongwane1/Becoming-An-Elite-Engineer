#include "mm/pool.hpp"
#include "mm/todo.hpp"
// Exercise 10, part 2 — implement the Pool. Notes: Lesson 10.3. Read include/mm/pool.hpp first.
namespace mm {

// PROVIDED: threads all count_ blocks onto the free list (called once by the ctor). Study it — your
// allocate()/deallocate() manipulate this same intrusive list (Lesson 10.3 §1).
void PoolAllocator::build_free_list() {
    free_head_ = nullptr;
    for (std::size_t i = count_; i-- > 0;) {                 // reverse so block 0 ends up at the head
        auto* node = reinterpret_cast<FreeNode*>(base_ + i * block_);
        node->next = free_head_;
        free_head_ = node;
    }
}

void* PoolAllocator::allocate() {
    // TODO(Lesson 10.3 §1): pop and return the head free block (nullptr if the list is empty).
    Todo("PoolAllocator::allocate");
}

void PoolAllocator::deallocate(void* /*p*/) {
    // TODO(Lesson 10.3 §1/§4): push the block back onto the head of the free list (nullptr => no-op).
    Todo("PoolAllocator::deallocate");
}

}  // namespace mm
