#pragma once
// Exercise 7 — build your own free-list allocator (split + coalesce). Notes: Lesson 7.
//
// You manage ONE fixed arena (a single aligned buffer obtained from the system once, in the ctor)
// and carve it into boundary-tagged chunks exactly as Lesson 7.2–7.4 describe. This is a miniature
// of glibc's main-arena behaviour: a single free list, first-fit search, splitting on allocate, and
// real neighbour coalescing on free.
//
// WHAT YOU IMPLEMENT lives in src/allocator.cpp (6 functions, each currently a Todo()):
//     align_up, find_fit, split, coalesce, allocate, deallocate
// Everything else here — the block layout, the navigation accessors, and the intrusive free-list
// push/remove — is PROVIDED so you spend your effort on the allocator logic, not on pointer casts.
//
// LAYOUT (all sizes are multiples of kAlign = 16, so every payload pointer is 16-byte aligned):
//
//     H (16-aligned)                               footer (16 B)
//     │                                              │
//     ▼                                              ▼
//     ┌───────────┬──────────────────────────────┬───────────┐
//     │  Header   │  payload (>= 16 B)            │  Footer   │
//     │ size|free │  user data, OR a FreeNode     │ size|free │
//     └───────────┴──────────────────────────────┴───────────┘
//     └──────────────── block size = Header.size ─────────────┘   (includes both tags)
//
//   * Header and Footer BOTH carry the block's total size and its free flag. The footer is the
//     "boundary tag" (Lesson 7.2 §5): it lets you find the PREVIOUS block in O(1) for backward
//     coalescing, by reading the footer that sits just below this block's header.
//   * A FREE block stores a FreeNode (prev/next links) in its payload — that's why the minimum
//     payload is 16 bytes (two pointers). An IN-USE block stores your data there instead.
//   * malloc returns the PAYLOAD pointer; the header is the 16 bytes just before it (Lesson 7.2 §4).

#include <cstddef>
#include <cstdint>
#include <new>

namespace mm {

// Per-block bookkeeping written at the start of every block. `size` is the WHOLE block size
// (header + payload + footer), always a multiple of 16. `free` is 1 if the block is free.
struct BlockHeader {
    std::size_t size;
    std::size_t free;
};
// Mirror tag at the end of every block (the boundary tag). Same two fields as the header.
struct BlockFooter {
    std::size_t size;
    std::size_t free;
};
// Intrusive doubly-linked free-list node, stored IN the payload of a free block.
struct FreeNode {
    BlockHeader* prev;
    BlockHeader* next;
};

class FreeListAllocator {
  public:
    static constexpr std::size_t kAlign = 16;                         // malloc's alignment here (Lesson 7.2 §3)
    static constexpr std::size_t kHeader = 16;                        // sizeof(BlockHeader), 16-aligned
    static constexpr std::size_t kFooter = 16;                        // sizeof(BlockFooter), 16-aligned
    static constexpr std::size_t kOverhead = kHeader + kFooter;       // 32 bytes of tags per block
    static constexpr std::size_t kMinPayload = 16;                    // must hold a FreeNode (two ptrs)
    static constexpr std::size_t kMinBlock = kOverhead + kMinPayload; // 48: smallest legal block

    // Grab `total_bytes` of memory from the system ONCE and lay it out as a single free block that
    // fills the whole arena. `total_bytes` is rounded down to a multiple of 16; it must leave room
    // for at least one minimum block. (This is your "ask the kernel for a slab" step, Lesson 7.1 —
    // we use operator new; a real allocator would sbrk/mmap.)
    explicit FreeListAllocator(std::size_t total_bytes);
    ~FreeListAllocator();
    FreeListAllocator(const FreeListAllocator&) = delete;
    FreeListAllocator& operator=(const FreeListAllocator&) = delete;

    // ===== YOU IMPLEMENT THESE (src/allocator.cpp) =====

    // Return a pointer to at least `n` usable payload bytes, 16-byte aligned, or nullptr if the arena
    // can't satisfy it. n==0 still returns a valid, freeable minimum allocation (Lesson 7.1 §5).
    // Steps (Lesson 7.3 §5): round up -> find_fit -> remove from free list -> split -> mark in use.
    void* allocate(std::size_t n);

    // Free a payload pointer previously returned by allocate (or nullptr: a no-op, Lesson 7.1 §5).
    // Steps (Lesson 7.4): find the block -> mark free -> push on the free list -> coalesce.
    void deallocate(void* p);

    // Helpers you also implement (allocate/deallocate call these). Declared here so your names match.
    //
    // find_fit: first-fit (Lesson 7.3 §2) — return the first free-list block with size >= need, or
    //           nullptr. (Walk the list via free_head()/node_of(h)->next.)
    BlockHeader* find_fit(std::size_t need);
    // split: precondition — h is big enough for `need` and is NOT on the free list. If the tail
    //        remainder (h->size - need) is a legal block (>= kMinBlock), shrink h to `need` and make
    //        the remainder a FREE block pushed to the list; otherwise leave h's size unchanged.
    //        Either way h's free flag is left for allocate() to set. (Lesson 7.3 §3.)
    void split(BlockHeader* h, std::size_t need);
    // coalesce: precondition — h is FREE and ON the free list. Merge it with any free physical
    //           neighbour (next and/or prev); keep the lowest-address header; fix the free list.
    //           Return the surviving (possibly merged) header. (All four cases, Lesson 7.4 §1.)
    BlockHeader* coalesce(BlockHeader* h);

    // ===== PROVIDED: layout + navigation (Lesson 7.2). Use these in your 6 functions. =====

    std::byte* base() const { return base_; }
    std::byte* end() const { return base_ + total_; }
    std::size_t total_bytes() const { return total_; }

    // Round n up to the next multiple of a (a is a power of two). (You implement this one.)
    static std::size_t align_up(std::size_t n, std::size_t a);

    // The total block size needed to serve a request of `n` payload bytes (rounded, with overhead).
    static std::size_t block_size_for(std::size_t n) {
        std::size_t payload = align_up(n == 0 ? 1 : n, kAlign);
        if (payload < kMinPayload) payload = kMinPayload;
        return payload + kOverhead;
    }

    static BlockHeader* header_of(std::byte* block_start) { return reinterpret_cast<BlockHeader*>(block_start); }
    static std::byte* payload_of(BlockHeader* h) { return reinterpret_cast<std::byte*>(h) + kHeader; }
    static BlockHeader* header_from_payload(void* p) {
        return reinterpret_cast<BlockHeader*>(static_cast<std::byte*>(p) - kHeader);
    }
    static BlockFooter* footer_of(BlockHeader* h) {
        return reinterpret_cast<BlockFooter*>(reinterpret_cast<std::byte*>(h) + h->size - kFooter);
    }
    static FreeNode* node_of(BlockHeader* h) { return reinterpret_cast<FreeNode*>(payload_of(h)); }
    static std::size_t payload_capacity(BlockHeader* h) { return h->size - kOverhead; }

    // Write BOTH tags of a block in one call: set its size and free flag consistently (header+footer).
    // Use this whenever you create, shrink, or re-flag a block so the boundary tag never drifts.
    static void set_block(BlockHeader* h, std::size_t size, bool is_free) {
        h->size = size;
        h->free = is_free ? 1 : 0;
        BlockFooter* f = footer_of(h);
        f->size = size;
        f->free = is_free ? 1 : 0;
    }

    // Physical neighbours (Lesson 7.2 §5). Return nullptr when there is no neighbour inside the arena.
    BlockHeader* next_block(BlockHeader* h) const {
        std::byte* n = reinterpret_cast<std::byte*>(h) + h->size;
        return (n < end()) ? reinterpret_cast<BlockHeader*>(n) : nullptr;
    }
    BlockHeader* prev_block(BlockHeader* h) const {
        std::byte* hb = reinterpret_cast<std::byte*>(h);
        if (hb <= base()) return nullptr;  // no block below this one
        BlockFooter* pf = reinterpret_cast<BlockFooter*>(hb - kFooter);
        return reinterpret_cast<BlockHeader*>(hb - pf->size);
    }

    // ===== PROVIDED: the intrusive free list. Call these; don't hand-edit the links. =====

    BlockHeader* free_head() const { return free_head_; }

    void fl_push(BlockHeader* h) {  // push onto the front of the free list
        FreeNode* n = node_of(h);
        n->prev = nullptr;
        n->next = free_head_;
        if (free_head_) node_of(free_head_)->prev = h;
        free_head_ = h;
    }
    void fl_remove(BlockHeader* h) {  // unlink h from the free list
        FreeNode* n = node_of(h);
        if (n->prev) node_of(n->prev)->next = n->next;
        else free_head_ = n->next;
        if (n->next) node_of(n->next)->prev = n->prev;
    }

    // ===== PROVIDED: introspection for the tests (walk the physical block list). =====

    std::size_t block_count() const {
        std::size_t c = 0;
        for (BlockHeader* h = header_of(base_); h; h = next_block(h)) ++c;
        return c;
    }
    std::size_t free_block_count() const {
        std::size_t c = 0;
        for (BlockHeader* h = header_of(base_); h; h = next_block(h)) c += (h->free ? 1 : 0);
        return c;
    }
    std::size_t largest_free_payload() const {
        std::size_t m = 0;
        for (BlockHeader* h = header_of(base_); h; h = next_block(h))
            if (h->free && payload_capacity(h) > m) m = payload_capacity(h);
        return m;
    }
    // Structural invariants from Lesson 7: blocks tile [base,end) exactly; every size is a 16-multiple
    // >= kMinBlock; header size/flag match the footer; and (because we coalesce on every free) no two
    // physically adjacent blocks are both free. Returns true iff the heap is well-formed.
    bool check_invariants() const {
        std::byte* cur = base_;
        bool prev_free = false;
        bool first = true;
        while (cur < end()) {
            BlockHeader* h = reinterpret_cast<BlockHeader*>(cur);
            if (h->size < kMinBlock || h->size % kAlign != 0) return false;
            if (cur + h->size > end()) return false;
            BlockFooter* f = footer_of(h);
            if (f->size != h->size || f->free != h->free) return false;
            bool cf = h->free != 0;
            if (!first && prev_free && cf) return false;  // two adjacent free blocks => not coalesced
            prev_free = cf;
            first = false;
            cur += h->size;
        }
        return cur == end();
    }

  private:
    std::byte* base_ = nullptr;
    std::size_t total_ = 0;
    BlockHeader* free_head_ = nullptr;
};

// ===== PROVIDED: ctor/dtor. The ctor is your "ask the system for a slab" step (Lesson 7.1): it
// obtains one 16-aligned buffer and lays it out as a single free block covering the whole arena,
// pushed onto the free list. You never edit this; your logic is in src/allocator.cpp. =====
inline FreeListAllocator::FreeListAllocator(std::size_t total_bytes) {
    total_ = total_bytes - (total_bytes % kAlign);  // floor to a multiple of 16
    if (total_ < kMinBlock) total_ = kMinBlock;
    base_ = static_cast<std::byte*>(::operator new(total_, std::align_val_t(kAlign)));
    BlockHeader* h = header_of(base_);
    set_block(h, total_, /*is_free=*/true);  // one big free block spanning the arena
    free_head_ = nullptr;
    fl_push(h);
}
inline FreeListAllocator::~FreeListAllocator() {
    ::operator delete(base_, std::align_val_t(kAlign));
}

}  // namespace mm
