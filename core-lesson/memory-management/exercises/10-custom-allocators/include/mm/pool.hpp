#pragma once
// Exercise 10, part 2 — the Pool (fixed-block) allocator. Notes: Lesson 10.3.
//
// Pre-cuts ONE buffer into `count` equal blocks threaded onto an INTRUSIVE free list (each free
// block stores the "next" pointer in its own first bytes — Lesson 7.2 §2 / 10.3 §1). allocate() pops
// the head, deallocate() pushes it back — both O(1), no fragmentation. You implement build_free_list()
// (called by the ctor), allocate(), and deallocate(); the ctor/dtor/introspection are provided.
#include <cstddef>
#include <cstdint>
#include <new>

namespace mm {

class PoolAllocator {
  public:
    struct FreeNode { FreeNode* next; };  // lives inside each FREE block

    // A pool of `count` blocks, each at least `block_size` bytes and `align`-aligned. The block is
    // rounded up so it can hold a FreeNode and meet the alignment (Lesson 10.3 §3).
    PoolAllocator(std::size_t block_size, std::size_t count,
                  std::size_t align = alignof(std::max_align_t))
        : block_(round_block(block_size, align)),
          count_(count),
          align_(align),
          base_(static_cast<std::byte*>(::operator new(round_block(block_size, align) * count,
                                                       std::align_val_t(align_for_new(align))))) {
        build_free_list();  // YOU implement this (below)
    }
    ~PoolAllocator() { ::operator delete(base_, std::align_val_t(align_for_new(align_))); }
    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;

    // ===== YOU IMPLEMENT (src/pool.cpp) =====

    // Thread all `count_` blocks onto the free list: for each i, treat base_ + i*block_ as a FreeNode
    // and link them (push each onto free_head_). Called once by the ctor.
    void build_free_list();

    // Pop the head free block and return it (as raw storage), or nullptr if the pool is empty.
    void* allocate();

    // Push a block (previously from allocate()) back onto the free list. nullptr is a no-op.
    void deallocate(void* p);

    // ===== PROVIDED =====
    std::size_t block_size() const { return block_; }
    std::size_t block_count() const { return count_; }
    std::size_t free_count() const {  // walk the free list
        std::size_t c = 0;
        for (FreeNode* n = free_head_; n; n = n->next) ++c;
        return c;
    }
    bool owns(const void* p) const {
        auto a = reinterpret_cast<std::uintptr_t>(p);
        auto b = reinterpret_cast<std::uintptr_t>(base_);
        return a >= b && a < b + block_ * count_ && (a - b) % block_ == 0;  // on a block boundary
    }

    FreeNode* free_head() const { return free_head_; }
    std::byte* block_at(std::size_t i) const { return base_ + i * block_; }

  private:
    static std::size_t round_up(std::size_t n, std::size_t a) { return (n + (a - 1)) & ~(a - 1); }
    static std::size_t round_block(std::size_t bs, std::size_t align) {
        std::size_t need = bs < sizeof(FreeNode) ? sizeof(FreeNode) : bs;  // must hold a FreeNode
        return round_up(need, align);                                       // and meet alignment
    }
    // operator new's align_val_t must be a valid (power-of-two) alignment; clamp tiny aligns to 16.
    static std::size_t align_for_new(std::size_t a) { return a < 16 ? 16 : a; }

    std::size_t block_;
    std::size_t count_;
    std::size_t align_;
    std::byte* base_;
    FreeNode* free_head_ = nullptr;
};

}  // namespace mm
