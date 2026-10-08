# Exercise 10 — A pool allocator + a `pmr::memory_resource`

Implement the functions until `./run.sh` prints `ALL TESTS PASSED`.
Notes: `../../notes/10-custom-allocators/`.

You build the three workhorses of Lesson 10 and wire the last one into the standard library:

1. **`Arena`** (`src/arena.cpp`, notes 10.2) — a bump/monotonic allocator over one owned buffer.
   - `allocate(n, align)` — align the absolute address, bump the offset, `nullptr` if it won't fit.
   - `reset()` — reclaim everything at once (offset → 0; no destructors).
2. **`PoolAllocator`** (`src/pool.cpp`, notes 10.3) — fixed-size blocks on an intrusive free list.
   - `allocate()` — pop the head free block (`nullptr` if empty).
   - `deallocate(p)` — push it back (`nullptr` is a no-op).
   - (`build_free_list()` is provided — study it; your two ops drive the same list.)
3. **`ArenaResource`** (`src/arena_resource.cpp`, notes 10.4) — a `std::pmr::memory_resource` over an
   `Arena`, so a real `std::pmr::vector` can draw from it.
   - `do_allocate(bytes, align)` — from the arena; throw `std::bad_alloc` if full.
   - `do_deallocate(...)` — no-op (monotonic arena).
   - `do_is_equal(other)` — identity (`this == &other`).

The headers (`include/mm/*.hpp`) provide every ctor/dtor and all introspection — read each header's
top comment first; that's where the design lives.

## What the tests check (Lesson 10 invariants)
- **Arena**: aligned, non-overlapping pointers; every requested alignment honoured; `used`/`remaining`
  track; `nullptr` when full; `reset()` reuses from the first address.
- **Pool**: starts all-free; distinct in-buffer blocks on block boundaries; `nullptr` when empty;
  freeing then allocating returns the **same** block (recycling); full allocate→free→reallocate cycle;
  written blocks never clobber each other.
- **pmr**: a `std::pmr::vector<int>` (and a `vector<pmr::string>`) backed by `ArenaResource` works and
  its storage lives **inside** the arena; `do_deallocate` is a no-op so the arena only grows;
  `is_equal` is identity; `do_allocate` throws `bad_alloc` when the arena is full.

All under AddressSanitizer + UBSan.

## Notes on the stub state
- Most unimplemented functions show as `[ TODO ]`. **`do_is_equal` shows as `[ FAIL ]`**, not
  `[ TODO ]`, because it is `noexcept` — a `Todo()` throw from a `noexcept` function would call
  `std::terminate`, so its stub returns a wrong value instead. Implement it (`this == &other`).
- Keep `Arena::allocate` consistent with how you report `used()`; keep pool `allocate`/`deallocate`
  symmetric (pop vs push the same list).

## Measure it
`bench/alloc_bench.cpp` uses *your* `Arena`/`PoolAllocator`/`ArenaResource` to reproduce the Lesson 10
measurements. Build **Release** (ASan skews timing):
```
cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release && cmake --build build-rel --target alloc_bench
./build-rel/alloc_bench
```
Record the result in `RESULTS.md`, then fill in `DECISIONS.md`.

Run the tests: `./run.sh` or `./run.sh <filter>` (e.g. `./run.sh pool`).
