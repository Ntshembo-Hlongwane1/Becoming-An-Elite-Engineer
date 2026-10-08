# Lesson 15 — Thread-Local Allocation & Safe Reclamation (Phase D)

> Status: complete. Exercise: `../../exercises/15-mpmc-queue/`.
>
> Lesson 14's SPSC ring was the *easy* lock-free structure: one writer per index, no CAS, no freeing.
> The moment you allow **multiple** producers or consumers, two hard problems appear that have sunk
> countless lock-free attempts. First, **contention**: many threads hammering one atomic (or one
> cache line) serialise and crawl — the fix is to keep state **thread-local** (the same reason
> `malloc` has per-thread caches, Lesson 7.3). Second, and far worse, **safe reclamation**: in a
> lock-free structure that allocates nodes, *when is it safe to `free` a node another thread might
> still be reading?* Get it wrong and you have a use-after-free; the naïve fix exposes the infamous
> **ABA problem**. This lesson teaches the problem and its real solutions (reference counting, hazard
> pointers, epoch-based reclamation), then has you build the pragmatic answer the industry actually
> ships: a **bounded MPMC queue** whose per-cell sequence numbers make it ABA-free and
> reclamation-free — correct for many producers and consumers, proven with ThreadSanitizer.

This is Phase D, Lesson 2 (concurrency), and the final exercise of the curriculum. It builds on
Lesson 14 (atomics, orderings, happens-before), Lesson 6.4 (false sharing), and Lesson 7.3 (tcache).

## Ground rules recap
Every claim is quoted or marked **(derived)** / **(measured)** (run on your VM — GCC 15.2.0, x86-64,
4 cores). Books: `[CIA §x]` Williams; `[MCCP §x]` Alheraki concurrency. New sources at the bottom.

## Read in order
1. `01-thread-local-allocation.md` — contention as the enemy; per-thread state avoids cache-line
   ping-pong; `thread_local`; why allocators keep per-thread caches/arenas (Lesson 7.3/10). Measured:
   `thread_local` ~400× a contended shared atomic.
2. `02-the-reclamation-problem.md` — the central lock-free question: when can you `free` a node? The
   Treiber-stack `pop` hazard (thread A reads a node, B frees it, A dereferences → UAF); the naïve
   "leak" non-answer and counting threads-in-pop `[CIA §7.2.2]`.
3. `03-the-aba-problem.md` — ABA: a value goes A→B→A, CAS "succeeds" on a logically different object
   `[MCCP §12]`; CAS checks equality, not history; tagged/versioned pointers.
4. `04-reclamation-schemes-and-mpmc.md` — reference counting, **hazard pointers** `[CIA §7.2.3]`,
   **epoch-based reclamation / RCU**; and the pragmatic escape — a **bounded MPMC queue** (Vyukov)
   with per-cell sequence numbers: no node freeing, no ABA. Measured: 3P/3C, 150k items, TSan-clean.
5. `05-security-view.md` — three hats: reclamation UAF, ABA as an exploitation primitive, double-free;
   and the defences.
6. `06-glossary.md`.

Then do the exercise: **a bounded lock-free MPMC queue** — implement `push`/`pop` with CAS and the
correct memory orderings so **many** producer threads and **many** consumer threads exchange items
correctly and **ThreadSanitizer reports no data race**. (Builds under TSan, like Lesson 14.)

## The one-paragraph picture
Scaling a lock-free structure past one-writer-per-index hits two walls. **Contention**: an atomic
touched by many cores bounces its cache line between them (Lesson 6.4), so a shared counter can be
*hundreds* of times slower than a `thread_local` one — which is why fast allocators (glibc's tcache,
Lesson 7.3; your Lesson-10 pools) keep **per-thread** free lists and only touch shared state rarely.
**Reclamation**: with multiple poppers, a node you just removed may still be referenced by another
thread mid-operation, so you cannot simply `delete` it — doing so is a use-after-free `[CIA §7.2.2]`.
Worse, if you free and *reuse* a node, a stale `compare_exchange` can see the node's address return to
its old value and succeed even though the structure changed underneath it — the **ABA problem**
`[MCCP §12]`, because CAS compares the bits, not the history. The real solutions track who's still
reading: **reference counting**, **hazard pointers** (a thread advertises the node it's about to
touch; others defer freeing it) `[CIA §7.2.3]`, or **epoch-based reclamation** (free only after every
thread has passed a grace period). The pragmatic industrial answer avoids the whole swamp: a **bounded
MPMC queue** over a fixed array where each cell carries a **sequence number** that advances each lap —
nothing is ever allocated or freed (no reclamation), and the per-cell sequence makes ABA impossible.
That is what you build.

## New sources introduced here (also appended to `SOURCES.md`)
| Key | Source |
|---|---|
| `[CIA §7.2.2]` | Williams — managing memory in lock-free structures; the reclamation problem; counting threads-in-pop. |
| `[CIA §7.2.3]` | Williams — hazard pointers (Maged Michael's technique). |
| `[MCCP §12]` | Alheraki — the ABA problem; CAS validates equality not history; versioning/epoch/stamp tags. |
| `[MCCP §11]` | Alheraki — lock-free/wait-free; CAS-loop progress and contention. |
| `[VYUKOV-mpmc]` | D. Vyukov, *Bounded MPMC queue* — the per-cell sequence-number design. https://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue |
| `[CPPREF-cas]` | cppreference, `std::atomic::compare_exchange_weak/strong`. https://en.cppreference.com/w/cpp/atomic/atomic/compare_exchange |
