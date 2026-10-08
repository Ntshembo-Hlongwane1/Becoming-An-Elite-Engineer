# Decisions (one line each: decision — why — source)

## interesting(): new-edge detection folded into a cumulative map — the core of coverage feedback (18.2 §3)

## mutate(): the op mix (flip/replace/grow) and a reproducible PRNG — why cheap matters (18.2 §4)

## fuzz_one(): the harness mapping bytes -> code under test; deterministic & stateless (18.3 §1)

## engine compiled WITHOUT coverage, target WITH -O0 coverage — why (callback recursion / per-byte edges) (18.2 §1)

## reproducer via death callback, not per-iteration file write — the ~100x I/O cost (18.2 §5)

## coverage-guided vs blind: measured <1s vs 20,000,000 iters no-crash — why feedback wins (18.1 §4)

## triage drill: minimized reproducer + ASan stacks + writeup (root cause -> impact -> fix) (18.4)
