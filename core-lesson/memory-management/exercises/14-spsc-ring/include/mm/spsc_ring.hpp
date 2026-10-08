#pragma once
// Exercise 14 — a lock-free single-producer / single-consumer ring buffer. Notes: Lesson 14.4.
//
// EXACTLY one thread calls push(), EXACTLY one (other) thread calls pop(). Under that restriction the
// structure needs no locks and no CAS — just one release/acquire pair in each direction (Lesson 14.3).
//
// You implement push() and pop() with the CORRECT memory orderings so that:
//   * a producer thread and a consumer thread exchange items correctly, and
//   * ThreadSanitizer reports NO data race.
// The structure, next(), capacity(), and the empty/full conventions are provided — read Lesson 14.4.
//
// Layout (Lesson 14.4 §1): buf_[Cap], producer index tail_, consumer index head_. empty == head==tail;
// full == next(tail)==head (one slot always unused, so full != empty). head_ and tail_ are on separate
// cache lines (alignas(64)) to avoid false sharing (Lesson 6.4 / 14.4 §1).
#include <array>
#include <atomic>
#include <cstddef>

namespace mm {

template <class T, std::size_t Cap>
class SpscRing {
    static_assert(Cap >= 2, "need at least 2 slots (one is reserved to tell full from empty)");

  public:
    SpscRing() = default;
    SpscRing(const SpscRing&) = delete;
    SpscRing& operator=(const SpscRing&) = delete;

    // Usable capacity (one slot is reserved). Provided.
    static constexpr std::size_t capacity() { return Cap - 1; }

    // ===== YOU IMPLEMENT (Lesson 14.4 §2) =====

    // Producer only. Return false if full, else store v and publish the slot. Correct orderings:
    //   load tail_ (relaxed — my own index); if next(tail)==head_.load(ACQUIRE) return false (full);
    //   buf_[tail] = v;  tail_.store(next, RELEASE)   // release PUBLISHES the written slot.
    bool push(const T& v) {
        // STUB: single-thread-correct, but uses memory_order_relaxed everywhere -> NO happens-before
        // is established, so the two-thread test is a DATA RACE on buf_ (ThreadSanitizer will flag it).
        // TODO(Lesson 14.4 §2): give the head_ load ACQUIRE and the tail_ store RELEASE.
        std::size_t t = tail_.load(std::memory_order_relaxed);
        std::size_t n = next(t);
        if (n == head_.load(std::memory_order_relaxed)) return false;   // TODO: acquire
        buf_[t] = v;
        tail_.store(n, std::memory_order_relaxed);                       // TODO: release
        return true;
    }

    // Consumer only. Return false if empty, else read into out and free the slot. Correct orderings:
    //   load head_ (relaxed — my own index); if head==tail_.load(ACQUIRE) return false (empty);
    //   out = buf_[head];  head_.store(next, RELEASE)  // release FREES the slot for the producer.
    bool pop(T& out) {
        // STUB: single-thread-correct, but relaxed everywhere -> racy across threads (see push()).
        // TODO(Lesson 14.4 §2): give the tail_ load ACQUIRE and the head_ store RELEASE.
        std::size_t h = head_.load(std::memory_order_relaxed);
        if (h == tail_.load(std::memory_order_relaxed)) return false;    // TODO: acquire
        out = buf_[h];
        head_.store(next(h), std::memory_order_relaxed);                 // TODO: release
        return true;
    }

    // ===== PROVIDED =====

    // Approximate emptiness (for single-threaded tests only; racy if called concurrently).
    bool empty_approx() const {
        return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
    }

  private:
    static std::size_t next(std::size_t i) { return (i + 1) % Cap; }

    std::array<T, Cap> buf_{};
    alignas(64) std::atomic<std::size_t> head_{0};   // consumer index (next to read)
    alignas(64) std::atomic<std::size_t> tail_{0};   // producer index (next to write)
};

}  // namespace mm
