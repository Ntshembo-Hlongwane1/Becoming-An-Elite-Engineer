# 16.5 — The Security Researcher's View: the sanitizer as microscope

Sanitizers are the single highest-leverage tool in memory-safety work — for *both* sides. A defender
runs them in CI to catch the Lesson-7.5/8.5/13.5/15.5 bug classes before release; an attacker/researcher
runs them under a fuzzer to *find* those same bugs at scale and get a precise, reproducible report
(faulting access + allocation stack) that turns "something's wrong" into "UAF of this object, allocated
here, freed here." This lesson is why you can read and trust those reports. Three hats.

## Offense — sanitizers as the discovery engine
- **Fuzzing under ASan is the modern bug-finding loop.** A fuzzer (libFuzzer/AFL++) drives inputs; ASan
  turns any out-of-bounds/UAF/double-free into an immediate, localized crash with stacks (§16.2). This
  is how a large fraction of real CVEs are now found. The researcher's skill is building the harness and
  reading the report — exactly this lesson.
- **The report is intelligence.** ASan/LSan tell you the bug class, the object, its size, and both the
  allocation and free sites. That's most of the triage done for you: it says which Lesson-7.5 primitive
  you have and where to look.
- **Sanitizer coverage maps the attack surface.** TSan finds the races (Lesson 14/15), MSan the
  uninitialized reads (an info-leak primitive — reading uninitialized heap can expose pointers/secrets,
  Lesson 7.5), ASan the spatial/temporal memory bugs. Running the right one is reconnaissance.

## The blind spots (what a researcher still hunts by hand)
Knowing what sanitizers *miss* is as important as what they catch — these survive a green CI:
- **Logic bugs that aren't memory errors:** ABA (Lesson 15.3 §3 — not a data race, TSan-invisible),
  `shared_ptr` reference **cycles** (Lesson 12.3 — LSan calls them "still reachable," not lost, §16.3
  §4), TOCTOU races whose window TSan didn't schedule.
- **Custom allocators blind ASan** (Lesson 10.5): a pool/arena carving one big `malloc`'d block hides
  intra-block overflow/UAF from ASan, because to ASan the whole block is one live allocation. Targets
  that ship custom allocators (browsers, engines, kernels) need their *own* instrumentation.
- **Reachable-but-wrong memory:** use of a stale-but-still-mapped value, missing bounds logic on data
  that's technically in-bounds, and anything the instrumentation doesn't model.
- **Not in production.** ASan's ~2× cost and memory overhead (§16.2) mean it's a test tool; shipped code
  relies on *hardening* (Lesson 17), not sanitizers.

## Offense — value, honest ceiling
A sanitizer doesn't create a primitive; it *reveals* one. The ceiling is whatever the underlying bug
gives (Lesson 7.5): a UAF ASan flags is still a UAF you must groom and escalate. The value is **speed
and precision of discovery** — ASan+fuzzing finds in hours what manual review finds in weeks, with a
report that pinpoints the object and sites. The honest statement separates "ASan found a UAF here" from
"and here's the controlled write it yields," which still needs allocator grooming + an info leak
(Lesson 7.5).

## Defense — run them, and know their edges
- **Sanitizers in CI, always.** ASan+UBSan on every test build (as this whole curriculum does), TSan on
  concurrency tests (Lessons 14–15), MSan where uninitialized reads matter, LSan for leaks. Fuzz the
  parsers/attack surface under ASan. This catches the majority of exploitable memory bugs pre-ship.
- **Build your own instrumentation where ASan is blind:** a custom allocator should carry its own
  red-zones/guard pages/quarantine and a leak table (this exercise + Lesson 17) — because ASan can't
  see inside it (Lesson 10.5). This is your capstone detector.
- **Don't mistake green for safe.** A clean ASan/TSan run does **not** prove absence of ABA, cycles,
  logic leaks, or custom-allocator bugs (the blind spots above). Pair sanitizers with review, model
  checking for lock-free code (Lesson 15.5), and leak-by-growth monitoring (RSS, Lesson 7.4).
- **Harden for production** (Lesson 17): sanitizers are for finding bugs in test; shipped defenses are
  canaries, guard pages, safe-linking, hardened allocators.

## Through-line to the capstone
Your capstone *is* a sanitizer: it intercepts allocation (§16.1, Lesson 8), keeps out-of-line metadata
with backtraces (§16.1/§16.4), red-zones and guard-pages allocations (§16.2, Lesson 17), and reports
UAF/overflow/leak with stacks. This lesson built the leak-tracking core and explained the shadow-memory
and reachability machinery you're emulating; Lesson 17 adds the overflow/UAF-catching hardening. Being
able to *build* the microscope is what lets you trust it, extend it where the stock tools are blind, and
read its reports like a researcher.

## My summary
