# Results

## bench/vector_bench — move vs copy on reallocation (Lesson 11.4)

Build Release and run:
```
cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release && cmake --build build-rel --target vector_bench
./build-rel/vector_bench
```

Reference run (this VM, GCC 15.2.0, 2026-10-08):
```
noexcept move type:      move noexcept=1  => relocations: copies=0 moves=1023
throwing move type:      move noexcept=0  => relocations: copies=1023 moves=0
```

### Reading it
- A `noexcept`-move element is **moved** on every reallocation (0 copies); a throwing-move element is
  **copied** (0 moves). Your `reserve` chose per type via `std::move_if_noexcept` — the same mechanism
  `std::vector` uses (Lesson 11.4 §3). This is the concrete payoff of marking moves `noexcept`.
- The notes' `m11.cpp` showed the same with `std::vector` (7 moves vs 7 copies over 8 elements) plus
  the strong-guarantee rollback and the `return std::move` / NRVO pessimization.

## Your Vector — notes
- the trickiest member (usually `reserve`'s rollback or the copy-and-swap assignment):
- did you keep both moves `noexcept`? what broke in the tests if you forgot?
