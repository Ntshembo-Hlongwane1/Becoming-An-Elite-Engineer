# 15.1 — Thread-local allocation: beating contention

## 1. Contention is the enemy

A correct atomic is not automatically a *fast* one. When many threads on many cores hammer a single
atomic — or even distinct variables that share a cache line — the line itself must bounce between the
cores' caches on every write (the coherence traffic of Lesson 6.1/6.4). This **contention** serialises
threads that were supposed to run in parallel: they take turns owning the line. `[MCCP §11]` warns that
under contention CAS loops retry repeatedly and "individual threads may make no observable progress."

### (measured) a contended atomic vs a thread-local counter
Four threads, 5,000,000 increments each, on your VM:
```
4 threads x 5000000 increments:  shared atomic 283–358 ms   thread_local 0.6–0.7 ms  (≈400–560x)
results: both == 20000000
```
Both produce the correct total (20,000,000), but the `thread_local` version is **~400–560× faster**
`(measured; tls.cpp)`. The shared `std::atomic` forces one cache line through four cores on every
`fetch_add` (ping-pong); the `thread_local` counter lives in each thread's own storage, touched by
only that thread, so there is no coherence traffic at all — each thread then adds its private subtotal
to the shared total **once** at the end. Same answer, three orders of magnitude apart.

## 2. The lesson: keep the hot path thread-local, touch shared state rarely

The design principle this teaches is everywhere in high-performance concurrent code:

> **Do the frequent work on per-thread state; synchronise with other threads only occasionally.**

- A **`thread_local`** variable has one independent instance per thread (its own storage), so updating
  it is a plain, uncontended memory write.
- You reconcile with the global picture infrequently — a batched `fetch_add` at the end (above), a
  periodic flush, or a merge at join.

This is exactly why the allocators you built earlier are structured the way they are:
- **glibc's tcache** (Lesson 7.3): each thread has a small *per-thread cache* of free chunks, so most
  `malloc`/`free` calls never touch the shared arena or take its lock — they pop/push a thread-local
  free list. The shared arena is the slow path, hit only when the tcache misses.
- **Per-thread pools/arenas** (Lesson 10): give each thread its own arena/pool and threads never
  contend on allocation at all; they only coordinate when returning memory across threads.

"Thread-local allocation" is this idea applied to memory management: **allocate from a per-thread
structure to avoid the lock/atomic contention a shared allocator would impose.** It's the single
biggest lever for allocator scalability, and it's the same lever as the counter above.

## 3. The flip side: cross-thread handoff still needs synchronization

Thread-local is free only while data *stays* in one thread. The moment a value crosses threads — a
produced item handed to a consumer, a chunk freed by a different thread than allocated it — you're back
to Lesson 14's rules (atomics, happens-before) and this lesson's harder problems (reclamation, §15.2).
The art is maximising the thread-local fast path and minimising the synchronised slow path. The MPMC
queue you build is precisely a *synchronised handoff* channel; you'd feed it from thread-local
producers and drain it to thread-local consumers.

## 4. `thread_local` caveats (so you use it correctly)
- **One instance per thread, constructed on first use per thread, destroyed at thread exit.** A
  `thread_local` with a non-trivial constructor/destructor runs them per thread.
- **Not a synchronization tool.** It *avoids* sharing; it doesn't order anything. Cross-thread
  visibility still needs atomics (§15.3/Lesson 14).
- **Cost:** access can be slightly more expensive than a plain global (an indirection through the
  thread's TLS block), but that's dwarfed by the contention it saves (§1).
- **Padding matters too:** even per-thread *array slots* can false-share if packed on one line — pad to
  `alignas(64)` (Lesson 6.4) when threads write adjacent entries.

## Drills
1. Reproduce `tls.cpp`. Then make the "thread_local" version instead write to a shared array indexed by
   thread id *without* padding, and re-measure — how much of the win disappears to false sharing
   (Lesson 6.4)? Add `alignas(64)` per slot and re-measure.
2. Explain, using §1 and Lesson 6.1, *why* the shared atomic is ~400× slower even though `fetch_add` is
   a single instruction. Where does the time actually go?
3. Relate glibc's tcache (Lesson 7.3) to §2: which `malloc` calls are the uncontended thread-local fast
   path, and which fall through to the shared arena? Why does that make multithreaded `malloc` scale?
4. You have 8 threads each producing into one shared MPMC queue. Is the queue's `enqueue_pos` atomic a
   contention point? How does batching (push several at once) or sharding (several queues) help?

## My summary
