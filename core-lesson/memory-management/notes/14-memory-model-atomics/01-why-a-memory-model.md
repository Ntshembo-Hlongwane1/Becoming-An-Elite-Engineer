# 14.1 — Why a memory model at all

## 1. One thread's intuition breaks across threads

In a single thread, memory behaves the way you expect: write `x`, read `x`, you get what you wrote, in
source order. With multiple threads on multiple cores, two things you can't see quietly break that:

- **Reordering.** The compiler and the CPU may execute a thread's memory operations in a *different
  order* than you wrote, as long as that thread's *own* observable behaviour is unchanged. Writing `a`
  then `b` in your source does not guarantee another thread sees `a`'s write before `b`'s.
- **Caches / visibility.** Each core has its own caches (Lesson 6.1). A value one core wrote may sit in
  its cache, not yet visible to another core, until coherence propagates it. `[MCCP §2.2]` covers this
  "cache coherency and visibility" directly.

So a second thread can observe your writes **out of order** or **late**. Any reasoning that assumes "I
wrote the flag after the data, so the reader sees the data once it sees the flag" is *wrong* without
explicit synchronization. The memory model exists to make that reasoning precise — to say exactly when
one thread is guaranteed to see another's writes.

## 2. The definition that governs everything: the data race

`std::vector`-level intuitions don't help here; the standard gives one hard rule. `[CIA §5.1.2]`:

> "If there's no enforced ordering between two accesses to a single memory location from separate
> threads, one or both of those accesses is not atomic, and one or both is a write, then this is a
> **data race** and causes **undefined behavior**." `[CIA §5.1.2]`

Read the three conditions — a data race is: (1) two accesses to the **same** memory location from
**different** threads, (2) **not ordered** relative to each other, (3) at least one is a **write** and
at least one is **non-atomic**. Hit all three and the program is UB — the whole program, not just that
variable. Williams does not mince words `[CIA §5.1.2]`:

> "once an application contains any undefined behavior, all bets are off; the behavior of the complete
> application is now undefined, and it may do anything at all." `[CIA §5.1.2]`

Crucially, there are exactly two ways to **avoid** the race `[CIA §5.1.2]`: (a) ensure only one thread
accesses the location at a time (a mutex — "one must happen before the other"), or (b) use **atomic
operations** to enforce an ordering. This lesson is about path (b).

### (measured) a plain flag handoff is a data race
Two threads: one does `data = 42; flag = 1;` (plain `int`s), the other spins `while(!flag){}` then reads
`data`. Under ThreadSanitizer on your VM:
```
WARNING: ThreadSanitizer: data race
SUMMARY: ThreadSanitizer: data race race.cpp:5 in operator()
```
`(measured; race.cpp)` The "obvious" flag handshake is UB: `flag` and `data` are non-atomic, written
by one thread and read by another with no enforced ordering. It might *appear* to work (especially on
x86) and then fail after an optimization, a compiler upgrade, or on another CPU — the worst kind of
bug. §14.2 fixes it.

## 3. Modification order: the one thing you do get for free

Even without synchronization, the model guarantees one thing per object `[CIA §5.1.3]`:

> "Every object in a C++ program has a defined **modification order** composed of all the writes to
> that object from all threads … in any given execution of the program all threads in the system must
> agree on the order." `[CIA §5.1.3]`

So all threads agree on the order of writes to a *single* object. But — and this is the catch —
"although all threads must agree on the modification orders of each individual object … they don't
necessarily have to agree on the relative order of operations on **separate** objects" `[CIA §5.1.3]`.
That disagreement about the relative order of operations on *different* variables is the whole
difficulty, and it's what memory orderings (§14.3) control.

## 4. What the memory model gives you
The model's job is to let you *create* ordering across threads on purpose. Its central concept is
**happens-before** (§14.3): if operation A happens-before operation B, then B is guaranteed to see A's
effects. Within a thread, source order gives happens-before. Across threads, you manufacture it with
synchronization — a mutex, or an atomic with the right memory ordering. Everything else in this lesson
is "how to establish happens-before between a producer and a consumer cheaply, without a lock."

## Drills
1. Reproduce `race.cpp` under TSan. Then make *only* `flag` atomic (leave `data` plain) — does TSan
   still complain? Why or why not? (Hint: §14.2/§14.3 — what ordering does an atomic flag establish?)
2. State the three conditions of a data race (§2). For each, give a change that removes the race by
   negating that one condition (different location / enforce ordering / make it a read / make it atomic).
3. The program "appears to work" on x86 but is still UB. Give two concrete reasons the same source
   could break later (compiler reordering at higher `-O`; a weakly-ordered CPU like ARM). Relate to §1.
4. Explain, using §3, why two threads can disagree about whether `x` was set before `y` even though
   each agrees on the order of writes to `x` alone and to `y` alone.

## My summary
