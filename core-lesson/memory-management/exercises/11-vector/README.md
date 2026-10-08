# Exercise 11 — A `Vector<T>` with the strong exception guarantee

Implement the six marked members in `include/mm/vector.hpp` until `./run.sh` prints `ALL TESTS PASSED`.
Notes: `../../notes/11-raii-and-rule-of-five/`.

`Vector<T>` is an owning, growable array built on **Lesson 8** (`operator new` for raw storage +
placement new to construct elements) and **Lesson 9** (alignment). Because it owns a raw allocation it
must get the **rule of five** right, and its growth must preserve the **strong guarantee**.

Being a template, everything is in the header. The header's top comment states the invariants; the
`PROVIDED` members (raw alloc/free, element access, `clear`, `swap`, destructor, `push_back`) are done
— read them. You implement the six `YOU IMPLEMENT` members:

1. **copy constructor** — deep, independent copy (Lesson 11.2).
2. **move constructor** (`noexcept`) — steal the buffer, leave the source empty (11.3).
3. **copy assignment** — copy-and-swap → strong guarantee + self-safe (11.4 §2).
4. **move assignment** (`noexcept`) — release, steal, null the source, self-move guard (11.3).
5. **`reserve`** — grow with `std::move_if_noexcept`, **strong guarantee** on a mid-relocation throw (11.4 §3).
6. **`emplace_back`** — grow if full, placement-new at the end, bump `size_` only on success (11.3).

## What the tests check (Lesson 11 invariants)
- value semantics: copy is a deep, independent duplicate; copy-assign is deep and self-assign-safe;
- move ctor/assign steal the exact buffer and leave the source empty & valid; self-move is safe;
- growth: `push_back`/`emplace_back`/`reserve` keep all elements correct across many reallocations;
  `at` throws `out_of_range`;
- **strong guarantee**: a `Bomb` whose copy throws mid-reallocation leaves the vector *exactly* as it
  was (size, backing pointer, and values) — for both `reserve` and `push_back`;
- **`move_if_noexcept`**: a `noexcept`-move element is **moved** on reallocation; a throwing-move
  element is **copied** (so the strong guarantee holds);
- move-only element types (`std::unique_ptr<int>`) work throughout;
- RAII: a live-instance counter returns to 0 (no leak); `Vector` is `nothrow` move
  constructible/assignable.

All under AddressSanitizer + UBSan.

## Notes on the stub state
- In the stub, every populating test stops at `[ TODO ] Vector::emplace_back` (you need it first).
  Implement `reserve` then `emplace_back`, and the rest of the tests begin to exercise copy/move.
- The **move ctor and move assignment are `noexcept`**, so their stubs can't use `Todo()` (throwing
  from `noexcept` calls `std::terminate`) — they're left as empty/no-op bodies. Keep the `noexcept`
  on them (the `vector_is_nothrow_movable` test depends on it) and fill in the stealing logic.
- `std::move_if_noexcept` (not plain `std::move`) in `reserve` is what earns the strong guarantee —
  re-read Lesson 11.4 §3 before writing it.

## Measure it
`bench/vector_bench.cpp` shows your `Vector` moving vs copying on reallocation (Release):
```
cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release && cmake --build build-rel --target vector_bench
./build-rel/vector_bench
```

Run: `./run.sh` or `./run.sh <filter>` (e.g. `./run.sh strong`). Then fill in `DECISIONS.md`.
