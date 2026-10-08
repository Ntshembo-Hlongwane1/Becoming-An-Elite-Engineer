#pragma once
// Exercise 16 — a mini leak detector that hooks allocation. Notes: Lesson 16.4.
//
// A LeakTracker keeps an OUT-OF-LINE, FIXED-CAPACITY side table mapping each live pointer -> {size,id}
// (Lesson 16.1): track() on allocate, untrack() on free. Whatever is still tracked is "outstanding"
// (a leak); untracking a pointer that isn't present is a double-/invalid-free (Lesson 16.3 §3).
//
// You implement the tracker logic: track, untrack, report, and the four counters. The fixed
// open-addressing table (with linear probing + tombstones), the mutex, the global instance, and the
// tracked_malloc/tracked_free hooks are PROVIDED — read them. Fixed storage means the detector never
// allocates through the allocator it hooks (no recursion, Lesson 16.1 §3).
#include <cstddef>
#include <cstdint>
#include <mutex>

#include "mm/todo.hpp"

namespace mm {

struct Leak {
    void* ptr;
    std::size_t size;
    std::uint64_t id;
};

class LeakTracker {
  public:
    static constexpr std::size_t kCapacity = 1u << 16;   // max simultaneously-live tracked blocks

    // ===== YOU IMPLEMENT (Lesson 16.4 §2) =====

    // Record a live allocation of `size` bytes at `p`. Bump live_count_, live_bytes_ += size,
    // total_allocations_. (Use the provided table: slot_for_insert(p) finds where to put it.)
    void track(void* p, std::size_t size) {
        std::lock_guard<std::mutex> lk(mu_);
        (void)p; (void)size;
        // TODO(Lesson 16.4 §2): i = slot_for_insert(p); store Slot{p,size,next_id_++,USED};
        //                       ++live_count_; live_bytes_ += size; ++total_allocations_.
        Todo("LeakTracker::track");
    }

    // Remove the live allocation at `p`. If present: drop it, --live_count_, live_bytes_ -= size,
    // return true (the caller may free). If NOT present (double-/invalid-free): ++invalid_frees_,
    // return false. (Use find_slot(p), which returns an index or kNpos.)
    bool untrack(void* p) {
        std::lock_guard<std::mutex> lk(mu_);
        (void)p;
        // TODO(Lesson 16.4 §2): i = find_slot(p); if kNpos -> ++invalid_frees_, return false
        //                       (double-/invalid-free); else drop it (st=TOMB), update counters, return true.
        Todo("LeakTracker::untrack");
    }

    // Copy up to `cap` still-live allocations into out[]; return the TOTAL number still live
    // (== live_count()). Scan the table for USED slots.
    std::size_t report(Leak* out, std::size_t cap) const {
        std::lock_guard<std::mutex> lk(mu_);
        (void)out; (void)cap;
        // TODO(Lesson 16.4 §2): scan for USED slots; write up to `cap` into out[]; return the total
        //                       number of live blocks (== live_count_).
        Todo("LeakTracker::report");
    }

    std::size_t live_count() const { std::lock_guard<std::mutex> lk(mu_); return live_count_; }
    std::size_t live_bytes() const { std::lock_guard<std::mutex> lk(mu_); return live_bytes_; }
    std::size_t total_allocations() const { std::lock_guard<std::mutex> lk(mu_); return total_allocations_; }
    std::size_t invalid_frees() const { std::lock_guard<std::mutex> lk(mu_); return invalid_frees_; }

    void reset() {   // provided: clear the table and counters (for tests)
        std::lock_guard<std::mutex> lk(mu_);
        for (std::size_t i = 0; i < kCapacity; ++i) slots_[i].st = EMPTY;
        live_count_ = live_bytes_ = total_allocations_ = invalid_frees_ = 0;
        next_id_ = 1;
    }

    // ===== PROVIDED: the fixed open-addressing table (linear probing + tombstones). =====
    enum State : std::uint8_t { EMPTY = 0, USED = 1, TOMB = 2 };
    struct Slot { void* key; std::size_t size; std::uint64_t id; State st; };
    static constexpr std::size_t kNpos = ~std::size_t{0};

    static std::size_t hash(void* p) {   // mix the pointer (drop the low alignment bits first)
        std::uint64_t x = reinterpret_cast<std::uintptr_t>(p) >> 4;
        x *= 0x9E3779B97F4A7C15ull;       // Fibonacci hashing
        return static_cast<std::size_t>(x) & (kCapacity - 1);
    }
    // Index of the USED slot whose key == p, or kNpos if not present.
    std::size_t find_slot(void* p) const {
        std::size_t i = hash(p);
        for (std::size_t n = 0; n < kCapacity; ++n, i = (i + 1) & (kCapacity - 1)) {
            if (slots_[i].st == EMPTY) return kNpos;              // a never-used slot ends the probe
            if (slots_[i].st == USED && slots_[i].key == p) return i;
            // TOMB: keep probing (a later insert may have passed through here)
        }
        return kNpos;
    }
    // Index where p should be inserted: the first EMPTY or TOMB slot on p's probe path (table isn't
    // full in this exercise). Returns kNpos only if the table is full.
    std::size_t slot_for_insert(void* p) const {
        std::size_t i = hash(p);
        for (std::size_t n = 0; n < kCapacity; ++n, i = (i + 1) & (kCapacity - 1))
            if (slots_[i].st != USED) return i;
        return kNpos;
    }

    // counters/table accessible to your method bodies above
    mutable std::mutex mu_;
    Slot slots_[kCapacity]{};                 // zero-init => all EMPTY
    std::size_t live_count_ = 0, live_bytes_ = 0, total_allocations_ = 0, invalid_frees_ = 0;
    std::uint64_t next_id_ = 1;
};

// The global tracker instance (provided).
LeakTracker& tracker();

// The allocation hooks (provided): wrap the real allocator and record metadata (Lesson 16.4 §1).
void* tracked_malloc(std::size_t n);
void tracked_free(void* p);

}  // namespace mm
