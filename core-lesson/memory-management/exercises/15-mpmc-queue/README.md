# Exercise 15 — A bounded lock-free MPMC queue (TSan-clean)

Implement `push`/`pop` in `include/mm/mpmc_queue.hpp` until `./run.sh` prints `ALL TESTS PASSED`.
Notes: `../../notes/15-reclamation-and-mpmc/`.

A **bounded multi-producer / multi-consumer** queue (Vyukov design, Lesson 15.4): a fixed power-of-two
array of cells, each carrying a **sequence number**; producers CAS `enqueue_pos_`, consumers CAS
`dequeue_pos_`. The per-cell sequence makes it **MPMC-correct and ABA-free** (Lesson 15.3), and nothing
is ever allocated or freed — so there is **no reclamation problem** (Lesson 15.2). This is the
pragmatic, industrial answer to "I need a correct lock-free queue."

## This exercise builds under ThreadSanitizer (not ASan)
`./run.sh` builds with `-fsanitize=thread` and `halt_on_error=1`: the point is race-freedom.

## The stub, and your job
The stub `push`/`pop` contain the full Vyukov algorithm (the CAS loop and the `seq`/`pos` comparison),
but the per-cell `seq` load/store use `memory_order_relaxed` (marked `TODO`). That is **single-thread
correct** (the single-thread tests pass) but establishes **no happens-before** for the cell's `data`,
so the MPMC test is a **data race** TSan flags. Your task (Lesson 15.4 §2 / Lesson 14.3):
- make the cell `seq` **load** `memory_order_acquire` (observe the other side's publish);
- make the cell `seq` **store** `memory_order_release` (publish the item / free the slot);
- the `enqueue_pos_`/`dequeue_pos_` CASes can stay `relaxed`.

Then the producer's `c.data = v` **happens-before** the consumer's `out = c.data`, per cell — correct
*and* TSan-clean. Understand *why* each ordering is needed, not just which compiles.

## What the tests check (Lesson 15 invariants)
- single-thread FIFO and full/empty (`capacity()==Cap`); wraparound over many laps;
- **4 producers + 4 consumers** transfer 100,000 unique items, each received **exactly once**
  (`consumed==TOTAL` and the `sum` matches) **and no ThreadSanitizer data race**;
- the SPSC special case still works.

## Why this design dodges the hard problems (Lesson 15.2–15.3)
- **No reclamation**: cells are reused in place, never freed — so there's no "when is it safe to free
  a node another thread is reading?" question (the Treiber-stack UAF of Lesson 15.2).
- **No ABA**: the per-cell sequence number is a version tag that advances every lap, so a stale
  producer/consumer can never mistake a recycled cell for a ready one (Lesson 15.3 §4).
- Note ThreadSanitizer would **not** catch an ABA/reclamation logic bug (those aren't data races,
  Lesson 15.3 §3) — this design engineers them out by construction.

Run: `./run.sh` or `./run.sh <filter>`. Then fill in `DECISIONS.md`.
