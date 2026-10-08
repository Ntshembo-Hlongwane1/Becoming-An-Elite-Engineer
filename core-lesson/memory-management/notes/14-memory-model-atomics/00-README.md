# Lesson 14 — The C++ Memory Model and Atomics (Phase D)

> Status: complete. Exercise: `../../exercises/14-spsc-ring/`.
>
> Phases A–C treated memory as something *one* thread touches. Real systems have many threads on many
> cores, each with its own caches and a CPU and compiler that **reorder** memory operations for speed.
> The moment two threads touch the same memory without coordination, you have a **data race** —
> which in C++ is not "sometimes wrong," it is **undefined behaviour**, the Lesson-7.5 kind. The C++
> **memory model** is the rulebook that says exactly when one thread is guaranteed to see another's
> writes, and **atomics** (`std::atomic`, with *memory orderings*) are the tools that buy those
> guarantees without a mutex. Get this right and you can build **lock-free** data structures that
> scale; get it wrong and you get corruption that appears only under load, on one machine, once a
> week. You'll build the canonical starter lock-free structure — a **single-producer /
> single-consumer ring buffer** — and prove it **race-free with ThreadSanitizer**.

This is Phase D, Lesson 1 (concurrency). It builds on Lesson 6 (caches, coherence, false sharing) and
all of Phases A–C. It is grounded in your two concurrency books plus `[MEM]`.

## Ground rules recap
Every claim is quoted from a cited source, or marked **(derived)** / **(measured)** (a program run on
your VM — GCC 15.2.0, x86-64). New sources are at the bottom and in `SOURCES.md`. Books:
`[CIA §x]` = Williams, *C++ Concurrency in Action* 2e; `[MCCP §x]` = Alheraki, *Modern C++
Concurrency and Parallel Programming*; `[MEM §x]` = the memory book.

## Read in order
1. `01-why-a-memory-model.md` — why reordering and caches make naïve sharing wrong; the **data race =
   UB** rule; modification order. Measured: TSan flags a plain-`int` flag handoff as a data race.
2. `02-atomics-and-data-races.md` — `std::atomic`, what "atomic" guarantees, fixing the race with an
   atomic; lock-free vs lock-based; what types can be atomic. Measured: the atomic version is TSan-clean.
3. `03-memory-ordering.md` — **happens-before** and **synchronizes-with**; the six orderings / three
   models (seq_cst, acquire-release, relaxed); a **release** store synchronizes-with an **acquire**
   load that reads it; when to use which; the x86 cost note.
4. `04-the-spsc-ring-buffer.md` — the design: head/tail indices, the one-empty-slot trick, **release
   publishes / acquire observes**, relaxed on your own index, `alignas(64)` against false sharing
   (Lesson 6.4), and why SPSC is lock-free (wait-free). Measured: 1,000,000 ints transferred, TSan-clean.
5. `05-security-view.md` — three hats: data races as UB/corruption/TOCTOU, torn reads, an ABA preview,
   finding races with TSan, and the defences.
6. `06-glossary.md`.

Then do the exercise: **a lock-free SPSC ring buffer** — implement `push`/`pop` with the correct
memory orderings so a producer thread and a consumer thread exchange a million items correctly and
**ThreadSanitizer reports no data race**. (This exercise builds and runs under **TSan**, not ASan.)

## The one-paragraph picture
A modern CPU/compiler may **reorder** a thread's memory operations and keep written values in a core's
cache that other cores can't see yet, so two threads sharing data without coordination can observe
impossible-looking orders — and if at least one access is a non-atomic write, that's a **data race**,
which is **undefined behaviour** `[CIA §5.1.2]`. The **memory model** defines **happens-before**: the
guarantee that if A happens-before B, then B sees A's effects. Within one thread, source order gives
happens-before; *across* threads you must create it with synchronization. `std::atomic` operations are
indivisible and carry a **memory ordering** that controls that cross-thread visibility: with
**acquire-release**, a `store(release)` on an atomic **synchronizes-with** a `load(acquire)` that reads
that value, establishing happens-before between the two threads `[CIA §5.3.3]` — so everything the
producer wrote *before* its release is visible to the consumer *after* its acquire. The strongest
ordering, **seq_cst** (the default), additionally gives a single total order all threads agree on;
**relaxed** gives atomicity but *no* ordering. A **lock-free** algorithm coordinates with atomics and
retries instead of locks, guaranteeing that *at least one thread always makes progress* `[MCCP §11.1.1]`.
The SPSC ring buffer is the simplest useful example: the producer publishes a slot with a release
store to `tail`, the consumer observes it with an acquire load of `tail`; the handoff is exactly one
release/acquire pair in each direction, no locks, no CAS.

## New sources introduced here (also appended to `SOURCES.md`)
| Key | Source |
|---|---|
| `[CIA §5.1.2]` | Williams, *C++ Concurrency in Action* 2e — data race = UB; objects, memory locations, modification orders. |
| `[CIA §5.3]` | Williams — synchronizes-with, happens-before, the six memory orderings / three models, seq_cst, acquire-release, relaxed. |
| `[CIA §7]` | Williams — designing lock-free data structures. |
| `[MCCP §5.1.2]` | Alheraki — acquire/release ordering; the release-acquire happens-before pair. |
| `[MCCP §11.1.1]` | Alheraki — lock-free = at least one thread makes forward progress in finite steps. |
| `[MCCP §2.2]` | Alheraki — cache coherency & false sharing (ties to Lesson 6.4). |
| `[CPPREF-atomic]` | cppreference, `std::atomic` and `std::memory_order`. https://en.cppreference.com/w/cpp/atomic/atomic , https://en.cppreference.com/w/cpp/atomic/memory_order |
