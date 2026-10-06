#pragma once
// M2 — Arena (bump allocator). Notes: Part 2 §2, §7, §8.
//
// Requirements:
//  * Blocks of kBlockSize (4096) bytes from `new char[]`; a request larger than kBlockSize/4 gets a
//    dedicated block of exactly that size (bounded waste — read LevelDB's comment on why).
//  * Allocate(n): no alignment promise (for byte strings). AllocateAligned(n): result aligned to
//    alignof(std::max_align_t). Use the power-of-two mask trick: addr & (align - 1).
//  * There is a subtle trap in AllocateAligned's "doesn't fit" path (I fell into it while writing
//    the reference solution). arena_aligned_after_odd_allocations will catch you; when it does,
//    explain the bug in DECISIONS.md.
//  * Never frees individual allocations. Destructor frees every block (RAII). Non-copyable,
//    non-movable (other objects hold raw pointers into it).
//  * MemoryUsage() may be called from other threads while one thread allocates: keep the counter in
//    a std::atomic<size_t> and use memory_order_relaxed (it's a statistic, not a synchronizer).
#include <atomic>
#include <cstddef>
#include <vector>

namespace lsm {

class Arena {
public:
    static constexpr std::size_t kBlockSize = 4096;

    Arena() = default;
    ~Arena();
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;
    Arena(Arena&&) = delete;
    Arena& operator=(Arena&&) = delete;

    char* Allocate(std::size_t bytes);         // bytes > 0
    char* AllocateAligned(std::size_t bytes);  // bytes > 0
    std::size_t MemoryUsage() const;           // total bytes obtained from new[] (+ bookkeeping)

private:
    // Suggested members (you may change them):
    char* alloc_ptr_ = nullptr;
    std::size_t alloc_bytes_remaining_ = 0;
    std::vector<char*> blocks_;
    std::atomic<std::size_t> memory_usage_{0};
};

}  // namespace lsm
