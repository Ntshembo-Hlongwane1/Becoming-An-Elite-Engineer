#pragma once
// Exercise 15 — a bounded lock-free MPMC queue (Vyukov). Notes: Lesson 15.4.
//
// MANY producers and MANY consumers. Fixed-size power-of-two array of cells, each carrying a SEQUENCE
// NUMBER. Producers CAS enqueue_pos_, consumers CAS dequeue_pos_; the per-cell sequence makes it
// MPMC-correct and ABA-free (Lesson 15.3–15.4), and nothing is ever allocated/freed -> no reclamation
// problem (Lesson 15.2).
//
// You implement push() and pop() — the CAS loop AND the memory orderings. The cell layout, the ctor
// that seeds seq=i, the mask, and capacity() are provided. Read Lesson 15.4 §2 for the algorithm.
//
// Cell protocol:
//   push: target = buf_[pos & mask]; let s = target.seq; dif = s - pos.
//         dif==0 -> cell ready: CAS enqueue_pos_ pos->pos+1; on success write data, seq.store(pos+1).
//         dif<0  -> full (return false).   dif>0 -> another producer moved on; reload pos, retry.
//   pop:  target = buf_[pos & mask]; let s = target.seq; dif = s - (pos+1).
//         dif==0 -> cell filled: CAS dequeue_pos_ pos->pos+1; on success read data, seq.store(pos+Cap).
//         dif<0  -> empty (return false).  dif>0 -> another consumer moved on; reload pos, retry.
// Orderings (Lesson 15.4 §2 / Lesson 14): the cell seq LOAD is ACQUIRE (observe the publish); the cell
// seq STORE is RELEASE (publish data / free the slot); the pos CASes can be RELAXED.
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace mm {

template <class T, std::size_t Cap>
class MpmcQueue {
    static_assert(Cap >= 2 && (Cap & (Cap - 1)) == 0, "Cap must be a power of two >= 2");

  public:
    MpmcQueue() {
        for (std::size_t i = 0; i < Cap; ++i) buf_[i].seq.store(i, std::memory_order_relaxed);
        enqueue_pos_.store(0, std::memory_order_relaxed);
        dequeue_pos_.store(0, std::memory_order_relaxed);
    }
    MpmcQueue(const MpmcQueue&) = delete;
    MpmcQueue& operator=(const MpmcQueue&) = delete;

    static constexpr std::size_t capacity() { return Cap; }

    // ===== YOU IMPLEMENT (Lesson 15.4 §2) =====
    // STUB NOTE: the algorithm below is complete but the cell `seq` load/store use memory_order_RELAXED
    // (marked TODO). That is single-thread correct (the single-thread tests pass) but establishes NO
    // happens-before for `c.data`, so the MPMC test is a DATA RACE on the item (ThreadSanitizer flags
    // it). Your job: make the seq LOAD acquire and the seq STORE release (both push and pop), so the
    // producer's write happens-before the consumer's read. Understand WHY each is needed (Lesson 14.3).

    bool push(const T& v) {
        std::size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
        for (;;) {
            Cell& c = buf_[pos & mask_];
            std::size_t seq = c.seq.load(std::memory_order_relaxed);           // TODO: acquire (observe)
            std::intptr_t dif = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos);
            if (dif == 0) {
                if (enqueue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    c.data = v;
                    c.seq.store(pos + 1, std::memory_order_relaxed);           // TODO: release (publish)
                    return true;
                }
            } else if (dif < 0) {
                return false;                                                  // full
            } else {
                pos = enqueue_pos_.load(std::memory_order_relaxed);            // retry
            }
        }
    }

    bool pop(T& out) {
        std::size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
        for (;;) {
            Cell& c = buf_[pos & mask_];
            std::size_t seq = c.seq.load(std::memory_order_relaxed);           // TODO: acquire (observe)
            std::intptr_t dif = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos + 1);
            if (dif == 0) {
                if (dequeue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    out = c.data;
                    c.seq.store(pos + Cap, std::memory_order_relaxed);         // TODO: release (free slot)
                    return true;
                }
            } else if (dif < 0) {
                return false;                                                  // empty
            } else {
                pos = dequeue_pos_.load(std::memory_order_relaxed);            // retry
            }
        }
    }

  private:
    struct Cell {
        std::atomic<std::size_t> seq;
        T data;
    };
    static constexpr std::size_t mask_ = Cap - 1;

    Cell buf_[Cap];
    alignas(64) std::atomic<std::size_t> enqueue_pos_{0};   // producers CAS this
    alignas(64) std::atomic<std::size_t> dequeue_pos_{0};   // consumers CAS this
};

}  // namespace mm
