# Exercise 6 — Caches (cache-aware transforms + a timing harness)

Implement `src/cache.cpp` until `./run.sh` prints `ALL TESTS PASSED`.
Notes: `../../notes/06-caches/`.

- **Tests** (`./run.sh`) check *correctness*: a blocked transpose must equal the naive one for every
  size and block (including ragged edges), transpose is an involution, and both sum orders agree.
  Deterministic pass/fail.
- **Timing** is in `bench/bench.cpp` (observed, not asserted — timings are noisy). Build Release and
  run it to reproduce the 4.8× (row vs col) and ~5× (false sharing) effects, then fill in `RESULTS.md`:
  ```
  cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release && cmake --build build-rel --target bench
  ./build-rel/bench
  ```

Contracts in `include/mm/cache.hpp`. ASan+UBSan on in the Debug test build.

Run: `./run.sh` or `./run.sh <filter>`.
