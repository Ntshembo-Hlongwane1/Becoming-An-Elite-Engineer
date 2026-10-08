# 15.5 — The Security Researcher's View: reclamation bugs and ABA

Lock-free memory management is where the subtlest, highest-value memory-safety bugs live. The
reclamation problem (§15.2) and ABA (§15.3) are *not* data races — ThreadSanitizer won't flag them —
so they survive the testing that catches ordinary concurrency bugs, and they land squarely in the
Lesson-7.5 use-after-free / double-free family. This is advanced bug-hunting territory (kernels,
allocators, lock-free libraries). Three hats.

## The bug classes
- **Premature reclamation → use-after-free.** A lock-free structure frees a node another thread is
  still dereferencing (§15.2) → UAF. The window is tiny and schedule-dependent, but in a hot path it
  recurs constantly and can be hammered.
- **ABA → structural corruption / UAF (§15.3).** A stale CAS succeeds against a reused address, splicing
  a freed node back into the structure, or losing nodes → corrupted invariants and use-after-free of
  the resurrected node. `[MCCP §12]`: "The atomic operation is correct, but the algorithm is not."
- **Double-free via lost ownership.** Two threads both believe they popped the same node (a CAS/
  reclamation bug) → both free it → the Lesson-7.5 double-free primitive.
- **Unbounded deferral → memory-exhaustion DoS.** The "leak until safe" schemes (threads-in-pop §15.2,
  or a stalled epoch §15.4) can defer freeing forever under load or if a thread is blocked → RSS grows
  without bound; an attacker who can pin a reader stalls reclamation deliberately.
- **Allocator interaction.** Address reuse is what *enables* ABA (§15.3), and fast allocators reuse
  addresses aggressively (tcache/fastbins, Lesson 7.3) — so a lock-free structure that's "fine in
  tests" with a slow allocator can corrupt under a fast one. The allocator and the reclamation scheme
  are coupled.

## Offense — discovery
- **Source/RE:** any hand-rolled lock-free structure (`atomic<Node*>`, CAS loops) — check how popped
  nodes are freed (immediate `delete` with multiple consumers = §15.2 UAF), and whether a CAS on a
  reused pointer lacks a version tag (= ABA, §15.3). Look for "we'll just leak / free later" comments,
  missing hazard-pointer/epoch machinery, and CAS on a bare pointer where the pointee can be recycled.
- **Dynamic:** **ASan** catches the UAF/double-free *once the bad interleaving occurs* — so stress
  testing with many threads, many cores, and an adversarial (fast-reusing) allocator is the hunt.
  Crucially **TSan does *not* find ABA or a reclamation logic bug** (they're not data races, §15.3) —
  those need reasoning, code review, or a model checker (CDSChecker, Loom-style tools). "Passes TSan"
  is *not* "lock-free-correct."
- **Forcing the window:** pin/deschedule the reader thread (CPU affinity, priority, signals) to widen
  the use-after-free window; drive allocations to make the freed node's address come back (ABA).

## Offense — value, honest ceiling
- **Ceiling:** reclamation UAF / ABA give the full Lesson-7.5 primitive — reclaim-and-reuse a node as
  attacker-controlled data → arbitrary read/write up to code execution. These are exactly the bug
  class behind many kernel privilege-escalation CVEs (lock-free/RCU-adjacent lifetime bugs).
- **Floor / obstacles:** the windows are small and probabilistic; winning the race reliably can need
  many cores, scheduler control, and allocator grooming (Lesson 7.5), plus hardening + ASLR to
  escalate. The honest report names the node, the two operations racing on its lifetime, the
  interleaving, and the allocator behaviour that makes reuse (ABA) reachable.

## Defense — and what removes the class
- **Avoid reclamation entirely where you can.** A **bounded array** structure (the exercise's MPMC
  queue) never frees nodes → no premature-reclamation UAF, and per-cell **sequence numbers** make ABA
  impossible by construction (§15.4). Prefer bounded, fixed-capacity channels for inter-thread handoff.
- **If node-based, use a vetted reclamation scheme**: hazard pointers (`std::hazard_pointer`, C++26),
  epoch/RCU, or ref-counting — never immediate `delete` with multiple readers (§15.2). Defer freeing
  until provably safe; by preventing reuse, this also kills ABA.
- **Tag against ABA** when CASing recyclable pointers: version/epoch/stamp the value (§15.3 §4),
  double-width CAS.
- **Don't hand-roll.** The strongest defence: use a reviewed library (folly, moodycamel, boost.lockfree,
  TBB) or a mutex-based queue. A correct blocking structure beats a subtly-wrong lock-free one, and
  lock-free code should be *small, isolated, and heavily reviewed*.
- **Bound deferral** to prevent the DoS: cap pending-free lists, make grace periods progress even if a
  thread stalls (hazard pointers degrade better than naive epochs here).
- **Tooling:** ASan + heavy multithreaded stress on many cores (for the UAF/double-free), TSan for the
  ordinary races, and *manual reasoning / model checking* for ABA and reclamation safety — because no
  sanitizer proves those.

## Through-line to the capstone
Your capstone detector finds UAF/double-free at runtime; this lesson is the concurrency *source* of
those — lifetime bugs that only appear under the right interleaving and the right allocator. And in RE
/ vuln research, lock-free and RCU-style lifetime bugs (premature reclamation, ABA) are a prestigious,
high-impact modern class — the exact "find novel bugs, not published CVEs" work you're aiming at. The
sequence-number discipline you use to build the MPMC queue correctly is the same lens that spots where
a target's lock-free code frees one step too early.

## My summary
