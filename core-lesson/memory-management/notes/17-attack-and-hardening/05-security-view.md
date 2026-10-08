# 17.5 — The Security Researcher's View: the whole loop, and its limits

This lesson *is* the security view, so this file is the synthesis: what the whole course was building
toward, what hardening actually buys, and the honest limits that keep memory-safety an arms race — the
mindset you carry into RE and 0-day work.

## The loop you now own: build → break → defend
Across the curriculum you did all three sides of every memory structure:
- **Built** it — allocator (7), `operator new` (8), arena/pool/pmr (10), `Vector`/smart-pointers/
  `SmallVector` (11–13), lock-free queues (14–15).
- **Broke** it — each lesson's security view traced the exact primitive the structure exposes
  (metadata corruption, UAF, double-free, pointer injection, ABA, TOCTOU, invalidation).
- **Defended** it — detection (16) and now hardening (17): canaries, safe-linking, guard pages,
  isolation.
Owning all three is the difference between someone who *uses* memory-safety tools and someone who can
*find novel bugs and design the mitigations* — the elite-RE / original-0-day-researcher goal. You can't
reliably attack a heap you couldn't build, and you can't harden one you can't attack.

## What hardening buys — and the three honest limits
Hardening is **economics, not proof**. It raises the attacker's cost; it rarely proves safety.
1. **Detection ≠ prevention.** Canaries and magics (§17.2) *detect* corruption at a moment (free);
   between the corruption and the check, the bug is live, and non-linear writes skip the tripwire. The
   policy response (abort) denies a retry but the bug existed. Only guard pages and isolation (§17.4)
   *prevent* (fault / impossible-by-construction) — and only for the shapes they cover.
2. **The attacker adapts.** Every mitigation birthed a bypass: NX → ROP; ASLR → info leaks; stack
   canaries → leak-the-canary / overwrite-past-it; safe-linking → leak the heap base. A single modern
   exploit chains a **leak** (defeat ASLR/safe-linking) with a **write** (the heap bug) precisely
   because each layer, alone, is bypassable. Hardening turns "1 bug → RCE" into "needs 2–3 bugs + a
   leak + grooming" — which stops the many and slows the few, but is not a wall.
3. **The only real fix is not having the bug.** Every defence here is a mitigation for a language
   without memory safety. The by-construction wins came from *higher up*: RAII and smart pointers
   (11–12) making UAF/double-free unrepresentable, bounds-carrying types (`span`/`at`, 1/13) killing
   overflow, isolation (§17.4) removing cross-type corruption, and ultimately memory-safe languages /
   the C++ safety profiles. Hardening defends the code you can't rewrite; it's the floor, not the goal.

## Offense — why a researcher learns defence
Knowing the mitigations *is* offensive knowledge: to exploit a modern target you must know which
defences are present and how each is bypassed (what leak defeats this ASLR, whether safe-linking is on,
where the guard pages are, which heap is partitioned). Reading a target's allocator — glibc version,
custom pool, PartitionAlloc — tells you which primitives survive. This lesson is the map of that
terrain. And building hardening yourself is how you learn to spot where a target's hardening is
*missing or wrong* (a custom pool with inline unmasked pointers, §17.1 §4) — the seam where novel bugs
live.

## Defense — the engineer's checklist
Pulling the course together, what you actually ship:
- **Don't own raw memory**: rule of zero, smart pointers, containers (11–13) — removes most UAF/
  double-free/leaks by construction.
- **Carry lengths / bounds-check**: `span`, `.at()`, the Lesson-1 integer discipline — removes overflow.
- **Use the hardened platform**: a modern allocator (hardened_malloc / PartitionAlloc / recent glibc),
  NX, ASLR/PIE, CFI, stack protector — on by default; keep them on.
- **Harden your own allocators** (this exercise) when you ship one: canaries, safe-linking,
  out-of-line metadata, guard pages, isolation — because ASan can't see inside them (10.5).
- **Find bugs before shipping**: ASan+UBSan+TSan+fuzzing in CI (16); review and model-checking for
  what sanitizers miss (ABA, cycles, logic — 15.5/16.5).
- **Assume breach**: poison secrets on free, fail closed (abort on detected corruption), minimize the
  attack surface and the lifetime of sensitive data.

## Through-line to the capstone
Your capstone detector is now fully specified by this course: intercept allocation (8/16), keep
out-of-line metadata with backtraces (16), red-zone/canary and **guard-page** each allocation (16/17),
quarantine freed memory and poison it (16/17), detect overflow/UAF/double-free, and report with a
stack unwind (5). You have built every piece. The capstone is their assembly — and the research skill
it trains (see a heap, reconstruct its rules, find where its invariants can be violated, and know what
the mitigations cost) is exactly the reverse-engineering / original-vulnerability-research capability
this whole curriculum was aimed at.

## My summary
