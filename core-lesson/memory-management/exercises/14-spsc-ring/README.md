# Exercise 14 — A lock-free SPSC ring buffer (TSan-clean)

Implement `push` and `pop` in `include/mm/spsc_ring.hpp` until `./run.sh` prints `ALL TESTS PASSED`.
Notes: `../../notes/14-memory-model-atomics/`.

A **single-producer / single-consumer** ring buffer: exactly one thread calls `push`, exactly one
other calls `pop`. Under that restriction it needs **no locks and no CAS** — just the right memory
orderings (Lesson 14.3–14.4). The structure, `next()`, `capacity()`, and the full/empty conventions
are provided; you choose the orderings on the four atomic operations.

## This exercise builds under ThreadSanitizer (not ASan)
The whole point is **race-freedom**, which ThreadSanitizer checks. `./run.sh` builds with
`-fsanitize=thread` and `halt_on_error=1`, so the first data race stops the run.

## The stub, and your job
The stub `push`/`pop` use `memory_order_relaxed` **everywhere**. That is **single-thread correct**
(the first three tests pass) but establishes **no happens-before** between the producer's slot write
and the consumer's slot read — so the two-thread test is a **data race on `buf_`** and TSan fails the
run. Your task (Lesson 14.4 §2):
- give the `head_`/`tail_` loads that observe *the other thread's* index `memory_order_acquire`;
- give the `tail_`/`head_` stores that publish a slot/free a slot `memory_order_release`;
- keep each thread's read of *its own* index `memory_order_relaxed`.

Get the release/acquire pairs right and the producer's write **happens-before** the consumer's read
(and vice-versa for freeing slots) — correct *and* TSan-clean.

## What the tests check (Lesson 14 invariants)
- single-thread FIFO order; `capacity()==Cap-1` (one slot reserved); correct full/empty reporting;
  wraparound over many cycles;
- **two threads transfer 200,000 items** with every value received exactly once (`sum` check) **and no
  ThreadSanitizer data race** — the test that the orderings exist to pass.

## Why relaxed "seems to work" but isn't
On x86 the relaxed stub may even produce the right `sum` (x86 is strongly ordered), yet it is still a
data race and **undefined behaviour** — TSan models happens-before, not x86 timing, so it flags it
regardless (Lesson 14.5). That gap is the lesson: prove race-freedom, don't observe it.

Run: `./run.sh` or `./run.sh <filter>`. Then fill in `DECISIONS.md`.
