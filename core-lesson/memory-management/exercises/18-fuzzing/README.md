# Exercise 18 — Build a coverage-guided fuzzer

Implement three functions until `./run.sh` prints `BUG FOUND AND REPRODUCED ✅`.
Notes: `../../notes/18-fuzzing/`.

You build the core of a coverage-guided fuzzer (the AFL/libFuzzer mechanism, Lesson 18.2) on plain
`g++` + ASan — no libFuzzer/AFL needed — and point it at a target with a 4-byte **magic gate**
(`"FUZZ"`) guarding a heap-buffer-overflow. A coverage-**blind** fuzzer can't crack the gate (~1 in
2³²); your coverage-**guided** one cracks it byte-by-byte and finds the bug in under a second.

## Build model (handled by run.sh)
- `src/engine.cpp` — the fuzzer: coverage bitmap, the `__sanitizer_cov_trace_pc` callback, a crash
  death-callback, and the loop. Compiled **without** `-fsanitize-coverage` (so the callback isn't
  instrumented and doesn't recurse — Lesson 18.2 §1).
- `src/target.cpp` — the code under test + your harness. Compiled **with** `-fsanitize-coverage=trace-pc
  -O0` (so each magic-byte check is a distinct edge that gives per-byte feedback — Lesson 18.1 §4).

## What you implement
1. **`interesting()`** (engine) — the feedback decision: return true iff this run hit an edge never
   seen before (folding new edges into the cumulative map). *This is the heart of coverage-guided
   fuzzing.* The stub returns `false` (coverage-blind) → the fuzzer is random → it never cracks the gate.
2. **`mutate()`** (engine) — perturb an input (bit flips / random bytes / grow), advancing a tiny PRNG
   so runs are reproducible. The stub does nothing → every input stays the seed.
3. **`fuzz_one()`** (target) — the harness: hand the fuzzer's bytes to the code under test
   (`parse(data, size)`). The stub does nothing → the target never runs → no coverage, no crash.

All three are needed: without the harness there's no coverage; without `mutate` nothing changes;
without `interesting` nothing is kept, so the gate is never cracked.

## What success looks like
`./run.sh` builds, fuzzes (budget 20M iters / 45s cap), and if a crash is found, **replays** the saved
`crash-input` under ASan to confirm it's real and deterministic. Implemented correctly it finds and
reproduces the overflow in a few seconds; the stub runs the full budget and reports `NOT FOUND ❌`.

Then do the triage drill (Lesson 18.4): minimize `crash-input` by hand, read the ASan stacks, and
write the finding up (root cause → impact → fix). And the harness drills (Lesson 18.3): point the same
engine at your Lesson-7 allocator / Lesson-11/13 containers with a bytes-as-bytecode harness + an
invariant oracle.

Run: `./run.sh` (or `./run.sh <budget>`). Then fill in `DECISIONS.md`.
