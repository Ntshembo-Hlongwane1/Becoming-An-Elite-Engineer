# 14.5 — The Security Researcher's View: data races and concurrency bugs

Concurrency is where memory bugs get *non-deterministic* — and non-determinism is an attacker's
friend, because a race window that triggers "rarely" can often be *forced* with load, scheduling
pressure, or precise timing. A data race is UB (Lesson 14.1), so every race is at least a latent
corruption bug; some are directly exploitable. Three hats.

## The bug classes
- **Data race → corruption / torn values.** Two threads touch a non-atomic location unsynchronized
  (Lesson 14.1). Beyond "wrong answer," a **torn** read/write of a pointer or size (a multi-word value
  updated non-atomically) can yield a half-old/half-new value → a wild pointer or a bogus length (the
  Lesson-1/Lesson-7.5 primitives, now reached via a race).
- **TOCTOU (time-of-check to time-of-use).** Thread A checks a condition (buffer not full, pointer
  non-null, index in range) and acts on it, but thread B invalidates it in the gap. The check and the
  use aren't atomic together → use-after-free, double-free, OOB. This is one of the most common
  *exploitable* concurrency bugs (kernel and browser CVEs are full of them).
- **Lock-free gone wrong.** Missing/weak orderings (a `relaxed` where you needed `release`/`acquire`)
  publish a pointer *before* the object it points to is visible → the reader dereferences
  uninitialised/garbage memory. And the **ABA problem** (`[MCCP §12]`, Lesson 15 preview): a CAS sees a
  value return to `A` and assumes nothing changed, but the object at `A` was freed and reallocated in
  between → use-after-free in a lock-free stack/queue.
- **Races defeating security checks.** A race on a flag/refcount/permission can skip a check or
  double-decrement a count (double-free), turning a logic race into memory corruption.

## Offense — discovery
- **Source/RE:** shared mutable state touched by more than one thread without a lock or atomic; a plain
  variable used as a cross-thread flag (Lesson 14.1); a check-then-act on shared state (TOCTOU);
  hand-rolled lock-free code with `memory_order_relaxed` on a publish; any CAS loop (inspect for ABA).
  Grep: globals/members mutated in thread bodies, `std::thread`/`std::async` lambdas capturing by
  reference, `volatile` misused as if it meant atomic (it does **not**).
- **Dynamic:** **ThreadSanitizer** is the primary tool — it reports data races with both access stacks
  (exactly as the measured examples here show). Races are schedule-dependent, so TSan (which models
  happens-before, not just timing) finds them even when they don't manifest on a given run; stress +
  many cores + TSan is the standard hunt. (ASan and TSan can't be combined — different builds.)
- **The "works on my machine" trap:** x86 is strongly ordered, so many races *appear* benign there and
  then corrupt on ARM/PowerPC or after an optimizer change — a reason real audits run TSan rather than
  trusting observed behaviour.

## Offense — value, honest ceiling
- **Ceiling:** a torn pointer or a TOCTOU use-after-free is the full Lesson-7.5 primitive — arbitrary
  reuse/write up to code execution — and the race often gives an attacker a *repeatable* trigger
  (hammer the window). ABA in a lock-free allocator/queue is a classic UAF. Kernel/browser race CVEs
  routinely reach RCE or privilege escalation.
- **Floor / obstacles:** many races are hard to *win* reliably (the window is tiny), need many cores or
  specific timing, and still face allocator hardening + ASLR (Lesson 7.5) to escalate. The honest
  report names the shared location, the two racing accesses, the trigger that widens the window, and
  what's still needed to turn the race into a controlled corruption.

## Defense — and what removes the class
- **Share nothing mutable, or synchronize all of it.** The two lawful options (Lesson 14.1 §2): a
  mutex around every access to shared mutable state, or `std::atomic` with the correct ordering.
  Immutable-after-publish data and message-passing (move ownership through a queue — your ring buffer!)
  avoid shared mutation entirely.
- **Use `std::atomic`, never `volatile`, for concurrency.** `volatile` prevents some compiler
  optimizations but provides **no** atomicity and **no** cross-thread ordering — using it as a thread
  flag is a data race. (A very common real bug.)
- **Prefer seq_cst until proven otherwise** (`[CIA §7.3]`); weaken to acquire-release only with a
  written happens-before argument; reserve `relaxed` for counters/own-indices. A wrong weakening is a
  silent, schedule-dependent corruption.
- **Make check-and-act atomic** to kill TOCTOU: one RMW (CAS/`fetch_*`) that both checks and acts, or
  hold the lock across both.
- **For lock-free code:** keep it tiny, audited, and SPSC/single-writer where possible (no CAS, no
  ABA); for multi-writer, use tagged pointers / hazard pointers / epochs against ABA (Lesson 15).
- **Tooling in CI:** ThreadSanitizer on all concurrent tests (this exercise builds under TSan), stress
  tests on many cores, and code review focused on "what's shared and how is it ordered."

## Through-line to the capstone
Your capstone's reporting and any multithreaded component must be TSan-clean; this lesson is where you
learn to *prove* race-freedom (happens-before) rather than hope for it. And in RE/exploit work,
concurrency bugs — TOCTOU, torn updates, ABA in lock-free code — are a rich, modern bug class: the
same memory-ordering reasoning you use to *build* the SPSC ring correctly is what you use to *spot*
where a target's lock-free code publishes a pointer one order too early.

## My summary
