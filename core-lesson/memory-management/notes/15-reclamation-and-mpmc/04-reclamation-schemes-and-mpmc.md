# 15.4 — Reclamation schemes, and the bounded MPMC queue

Two ways out of the §15.2/§15.3 swamp: **track who's still reading** (reclamation schemes), or **never
allocate/free nodes** (bounded array structures). The exercise takes the second — but know both.

## 1. Reclamation schemes (for node-based lock-free structures)

- **Reference counting** (`atomic<shared_ptr>` or a hand-rolled split count). Correct and simple;
  every node access is an atomic refcount RMW, so it's often the *slowest* (Lesson 12.4 contention).
  Good when node traffic is modest.
- **Hazard pointers** `[CIA §7.2.3]` (Maged Michael). A thread about to dereference a node first
  publishes it in a per-thread **hazard pointer** — "if a thread is going to access an object that
  another thread might want to delete, it first sets a hazard pointer to reference the object, thus
  informing the other thread that deleting the object would indeed be hazardous" `[CIA §7.2.3]`. To
  free a node, a thread scans all hazard pointers; if none reference it, free it, else defer. Precise
  (per-node), bounded memory, but O(threads) scan per reclaim and fiddly to implement. (C++26
  standardises `std::hazard_pointer`.)
- **Epoch-based reclamation (EBR) / RCU.** Threads announce an **epoch** on entering a critical
  section; a retired node is freed only after every thread has advanced past the epoch in which it was
  retired (a **grace period**) — by then no one can hold an old reference. Very low per-operation cost
  (just bump a thread-local epoch, Lesson 15.1), at the cost of delayed, batched freeing and
  memory-usage spikes if a thread stalls. RCU is the kernel's workhorse version.

All three share one idea: **defer freeing until provably safe**, where "safe" is defined by reference
counts, hazard announcements, or grace periods. And by preventing premature reuse, they also kill ABA
(§15.3) for free.

## 2. The pragmatic escape: a bounded queue that never frees

The cleanest way to avoid reclamation *and* ABA is to avoid node allocation entirely: a **fixed-size
array** structure. Nothing is `new`/`delete`d during operation, so there's no "when to free?" question
and no address reuse to cause ABA. The classic is **Vyukov's bounded MPMC queue** `[VYUKOV-mpmc]` — the
exercise.

### The design
A power-of-two array of cells, each with a **sequence number**, plus two positions:

```cpp
struct Cell { std::atomic<size_t> seq; T data; };
Cell  buf_[Cap];                 // Cap is a power of two
std::atomic<size_t> enqueue_pos_{0};   // producers CAS this
std::atomic<size_t> dequeue_pos_{0};   // consumers CAS this
// ctor: buf_[i].seq = i  for all i
```

**push** (any producer): read `enqueue_pos`; the target cell is `buf_[pos & (Cap-1)]`. Compare the
cell's `seq` to `pos`:
- `seq == pos` → the cell is **ready for this lap**. CAS `enqueue_pos` from `pos` to `pos+1` to claim
  the slot; on success, write `data`, then `seq.store(pos+1)` to mark "filled."
- `seq <  pos` → the cell is still full from a previous lap (consumer hasn't drained it) → queue
  **full**, return false.
- `seq >  pos` → another producer already advanced; reload `enqueue_pos` and retry.

**pop** (any consumer): symmetric against `dequeue_pos`, comparing `seq` to `pos+1`; on success read
`data`, then `seq.store(pos + Cap)` to mark the cell **ready for the next lap** (its sequence jumps a
full lap ahead).

### Why it's correct and ABA-free
- **MPMC via CAS on the positions**: many producers race to claim slots by CASing `enqueue_pos`; the
  loser just retries with the new position. Same for consumers on `dequeue_pos`. No locks.
- **Per-cell sequence = tagging (§15.3 §4)**: a cell's `seq` increases by `Cap` every lap and encodes
  *which lap* it's on. A producer only writes when `seq == pos`, which is true for exactly one producer
  on exactly one lap — a stale producer whose `pos` is a lap behind sees `seq > pos` and backs off. The
  "same slot" is never confused with "same state," so **ABA cannot occur** — the sequence number is the
  history that bare pointers lacked.
- **No reclamation**: cells are reused in place, never freed, so there is no §15.2 lifetime question.
- **The data handoff is ordered by `seq`**: the producer's `data` write is published by
  `seq.store(release)`; the consumer reads `seq` with `acquire` before reading `data` — the Lesson-14
  release/acquire pair, per cell, giving happens-before so the consumer never sees a torn/stale item.
  (These are the orderings you fill in.)

### (measured) 3 producers, 3 consumers, TSan-clean
```
MPMC 3P/3C transferred 150000 items: sum=11249925000 expected=11249925000 OK
```
`(measured; mpmc.cpp)` 150,000 items across 3 producers and 3 consumers, every item received exactly
once, and **no ThreadSanitizer data race** — the per-cell release/acquire pairs provide exactly the
happens-before, and the sequence numbers keep it MPMC-correct and ABA-free with no freeing.

## 3. Choosing
- **Bounded, known capacity, fixed-size items** → Vyukov bounded MPMC (no reclamation, no ABA, no
  allocation on the hot path). The default for inter-thread queues; the exercise.
- **Unbounded / node-based** (lock-free list/stack/tree) → you *must* pick a reclamation scheme:
  hazard pointers or EBR for performance, ref-counting for simplicity. Expect this to be the hard part.
- **When in doubt, don't hand-roll.** Prefer a vetted library (folly, moodycamel, boost.lockfree, TBB)
  or a mutex + condition variable — a correct blocking queue beats a subtly-broken lock-free one.

## Drills
1. Implement `push`/`pop` with CAS and the correct orderings (the exercise). Run 4P/4C under TSan and
   confirm correctness + race-freedom. Then weaken a `seq` store to `relaxed` and show TSan flags the
   `data` access.
2. Trace two producers racing on the same `enqueue_pos`: show how the CAS makes exactly one win and the
   other retry, and why no item is lost or duplicated.
3. Argue, using §2, why the sequence-number scheme makes ABA impossible here even though addresses
   (cells) are reused every lap. What plays the role of the §15.3 "version counter"?
4. Sketch how you'd add epoch-based reclamation to a lock-free *linked* queue instead. What does each
   thread announce, and when is a retired node finally freed?

## My summary
