# 18.5 — The Security Researcher's View: fuzzing is the discovery engine

Fuzzing is where "I understand memory bugs" (Phases A–E) becomes "I *find* them." For a
systems/security engineer it is the single most productive skill in the toolbox, used by both the
defender (fuzz your own attack surface before shipping) and the researcher (fuzz a target to find
exploitable bugs). This lesson's whole point is that you now understand it *mechanically* — so you can
build harnesses, read results, and extend it where stock tools stop. Three hats.

## Offense — fuzzing as the modern bug-finding loop
- **It's how CVEs are found now.** Coverage-guided fuzzer + sanitizer is the dominant discovery method
  for memory bugs in widely-used C/C++ (browsers, media/codec libraries, parsers, kernels). Google's
  **OSS-Fuzz** `[OSSFUZZ]` runs this continuously across thousands of projects and has found tens of
  thousands of bugs; offensive researchers run the same loop against targets of interest.
- **The researcher's leverage is the harness, not the engine.** libFuzzer/AFL++ are commodities;
  *finding the bug* depends on a harness that reaches the interesting code with the right input shape
  (§18.3) and a seed corpus/dictionary that gets past the checks. Writing good harnesses for a target
  you're researching — often by reverse-engineering its input format (Lesson 20) — is the skill.
- **Structure-aware & differential fuzzing** find what naive fuzzing can't: a grammar/protocol-aware
  mutator reaches deep states; a differential harness (your impl vs a reference, §18.3) finds
  correctness bugs that become security bugs (parser differentials → request smuggling, auth bypass).

## The limits (what fuzzing won't hand you)
- **Only what the harness reaches.** Unharnessed code is invisible; a shallow harness finds shallow
  bugs. Coverage plateaus are where manual review / symbolic execution take over.
- **Crash ≠ exploit.** The fuzzer gives a reproducer and a primitive; weaponizing it is the separate
  work of Lesson 19 (and still gated by hardening + leaks, Lesson 17.1 §5).
- **Blind spots carry over from the oracle:** no sanitizer for it → not found. ABA / logic / cycles
  (Lessons 15.3/12.3) need custom oracles or won't surface. Races need a concurrency fuzzer + TSan.
- **Custom allocators blind the sanitizer** (Lesson 10.5): fuzzing a program on its own pool misses
  intra-arena overflow unless you fuzz *with* your Lesson-17 canaries/guard-page allocator as the
  oracle — a concrete reason to own that tooling.

## Offense — value, honest ceiling
Fuzzing's value is **discovery speed and reproducibility**: it converts weeks of review into hours of
compute and hands you a deterministic reproducer + stacks. The ceiling is the underlying bug's
ceiling (Lesson 7.5) — the fuzzer reveals the primitive, it doesn't escalate it. The honest report
(the §18.4 writeup) states the primitive and what still gates RCE; "the fuzzer found a UAF" is the
beginning of the assessment, not the end.

## Defense — fuzz your own attack surface
- **Fuzz every parser of untrusted input, in CI, continuously.** Any code that touches network/file/
  user input is an attack surface; a fuzz target + ASan/UBSan in CI (and OSS-Fuzz if open source) is
  table stakes in 2026. Your media-stack's demuxers/parsers (the capstone target) are exactly this.
- **Add invariant/differential oracles** (§18.3) so fuzzing checks correctness, not just crashes.
- **Keep every reproducer as a regression test** (§18.4 §5) — fuzzing finds it once; the test keeps it
  dead.
- **Harness your own allocators/containers** (the drills) — the fastest way to discover an exercise you
  "finished" still has an edge case, and the habit that keeps your own code honest.

## Through-line to the capstone and the rest of Phase F
Fuzzing is the *discovery* stage of the research loop: it produces the reproducer you then **weaponize**
(Lesson 19: turn the found overflow into a controlled write against your own allocator), **analyze in
the binary** (Lesson 20: read the crash in the disassembly), and **defend** (Lesson 17 hardening +
Lesson 21 detector in CI). You built the fuzzer's coverage core yourself, so you understand *why* it
finds what it finds and where it goes blind — which is exactly what lets you point it at a real target
and trust the result.

## My summary
