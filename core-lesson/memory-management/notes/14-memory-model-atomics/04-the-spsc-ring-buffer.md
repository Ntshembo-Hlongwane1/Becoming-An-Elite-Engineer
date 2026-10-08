# 14.4 — The SPSC lock-free ring buffer

Now build the canonical lock-free structure. **SPSC** = single producer, single consumer: *exactly
one* thread ever calls `push`, *exactly one* (different) thread ever calls `pop`. That restriction is
what makes it so simple — it needs no CAS, just one release/acquire pair in each direction.

## 1. The structure

A fixed array and two indices:

```cpp
template <class T, std::size_t Cap>
class SpscRing {
    std::array<T, Cap> buf_{};
    alignas(64) std::atomic<std::size_t> head_{0};   // consumer's index (next to read)
    alignas(64) std::atomic<std::size_t> tail_{0};   // producer's index (next to write)
    static std::size_t next(std::size_t i) { return (i + 1) % Cap; }
};
```

- **`tail_`** is written only by the producer, read by both. **`head_`** is written only by the
  consumer, read by both. That single-writer-per-index property is why SPSC needs no atomic
  read-modify-write — each index has one owner.
- The buffer is **empty** when `head_ == tail_` and **full** when `next(tail_) == head_`. This leaves
  **one slot always unused**, so the two conditions are distinguishable (otherwise "full" and "empty"
  both look like `head == tail`). Usable capacity is therefore `Cap - 1`.
- `alignas(64)` puts `head_` and `tail_` on **separate cache lines** — otherwise the producer writing
  `tail_` and the consumer writing `head_` would fight over one line (**false sharing**, Lesson 6.4 /
  `[MCCP §2.2]`), serialising the two threads. One line per index keeps them independent.

## 2. push and pop — where the orderings go

```cpp
bool push(const T& v) {                                    // PRODUCER only
    std::size_t t = tail_.load(std::memory_order_relaxed);  // my own index -> relaxed
    std::size_t n = next(t);
    if (n == head_.load(std::memory_order_acquire)) return false;  // full? observe consumer's frees
    buf_[t] = v;                                            // write the slot (plain)
    tail_.store(n, std::memory_order_release);              // PUBLISH the slot
    return true;
}
bool pop(T& out) {                                         // CONSUMER only
    std::size_t h = head_.load(std::memory_order_relaxed);  // my own index -> relaxed
    if (h == tail_.load(std::memory_order_acquire)) return false;  // empty? observe producer's publishes
    out = buf_[h];                                          // read the slot (plain)
    head_.store(next(h), std::memory_order_release);        // FREE the slot
    return true;
}
```

Walk the synchronization, because every ordering tag is deliberate (§14.3 §4):

- **Producer publishes:** it writes `buf_[t]` (plain), then `tail_.store(n, release)`. The release means
  the slot write cannot be reordered after the store and becomes visible to any thread that acquires
  this `tail_` value.
- **Consumer observes:** `tail_.load(acquire)`. When it reads the new `tail_`, the release/acquire pair
  **synchronizes-with** → the producer's `buf_[t] = v` **happens-before** the consumer's `out = buf_[h]`.
  So the consumer reads the fully-written slot, never a torn or stale one. This is the §14.2 handoff,
  applied per element.
- **Symmetrically**, the consumer's `head_.store(release)` publishes "this slot is free," and the
  producer's `head_.load(acquire)` observes it before overwriting — so the producer never clobbers a
  slot the consumer hasn't finished reading.
- **Each index read with `relaxed` is a thread reading its *own* index** (`tail_` in `push`, `head_` in
  `pop`), which it alone writes — no cross-thread publish happens through that read, so relaxed is
  enough (§14.3 §5).

Two release/acquire pairs, two relaxed self-reads. No lock, no CAS.

## 3. Why it's lock-free (actually wait-free)

`[MCCP §11.1.1]`: lock-free means "at least one thread makes forward progress in a finite number of
steps … Lock-free algorithms avoid mutual exclusion primitives and rely on atomic operations." The
SPSC ring is stronger: each `push`/`pop` is a bounded number of steps with **no retries and no
spinning on contention** (a full `push` or empty `pop` just returns `false`) — it is **wait-free**.
There's no CAS loop because each index has a single writer, so there's nothing to retry.

## 4. (measured) a million items, TSan-clean

Producer pushes `0..999999`, consumer pops them all and sums, on your VM, built with ThreadSanitizer:
```
received sum=499999500000 expected=499999500000  OK
```
`(measured; ring.cpp)` All 1,000,000 integers transferred with the correct values **and no
ThreadSanitizer data-race warning** — the two release/acquire pairs provide exactly the happens-before
needed, and nothing more. (Swap any of them to `relaxed` and TSan flags the `buf_` access as a race,
because the slot write/read loses its happens-before — try it; it's the exercise's failure mode.)

## 5. The restriction matters
This is **single** producer / **single** consumer. With two producers, `tail_` has two writers → they'd
clobber each other's slot index and you'd need a CAS loop (MPSC/MPMC, Lesson 15) plus the **ABA**
pitfall (`[MCCP §12]`). The beauty of SPSC is that the single-writer-per-index invariant removes all of
that. Know the boundary: this structure is correct *only* under that restriction, and using it with
multiple producers or consumers is a data race.

## Drills
1. Implement `push`/`pop` with the orderings above and run the two-thread test under TSan. Then change
   the producer's `tail_.store` to `relaxed` and show TSan now reports a race on `buf_`. Explain which
   happens-before edge disappeared.
2. Remove the `alignas(64)` and benchmark throughput vs the aligned version (Lesson 6.4). How much does
   false sharing between `head_` and `tail_` cost on your machine?
3. Why does the "one empty slot" convention (usable capacity `Cap-1`) let you tell full from empty with
   just two indices? Design the alternative (a separate `count` atomic) and say why it's worse for SPSC.
4. Exactly what breaks if two threads call `push` concurrently on this structure? Trace two interleaved
   producers and show the lost/overwritten element (and why §5's single-writer invariant is essential).

## My summary
