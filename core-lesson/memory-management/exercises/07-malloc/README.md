# Exercise 7 — Build your own free-list allocator (split + coalesce)

Implement the 6 functions in `src/allocator.cpp` until `./run.sh` prints `ALL TESTS PASSED`.
Notes: `../../notes/07-how-malloc-works/`.

You manage **one fixed arena** (a single 16-aligned buffer the ctor takes from the system) and carve
it into boundary-tagged chunks exactly as Lesson 7.2–7.4 describe — the honest core of what glibc's
main arena does: one free list, first-fit, split on allocate, coalesce on free.

## What you implement (everything else is provided in `include/mm/allocator.hpp`)
1. `align_up(n, a)` — round `n` up to a multiple of the power-of-two `a` (Lesson 7.2 §3).
2. `find_fit(need)` — first-fit scan of the free list for a block whose `size >= need` (7.3 §2).
3. `split(h, need)` — if the remainder is `>= kMinBlock`, carve it off as a free block (7.3 §3).
4. `coalesce(h)` — merge a just-freed block with any free physical neighbour(s): all four cases (7.4 §1).
5. `allocate(n)` — round up → find_fit → remove from list → split → mark in use → return payload.
6. `deallocate(p)` — find block → mark free → push on list → coalesce. `deallocate(nullptr)` is a no-op.

The provided header gives you the block layout, the navigation accessors (`next_block`, `prev_block`,
`footer_of`, `payload_of`, `header_from_payload`, …), `set_block` (writes header **and** footer so the
boundary tag never drifts), and the intrusive free list (`fl_push` / `fl_remove`). Read its comment
block first — the diagram there is the whole exercise.

## What the tests check (Lesson 7 invariants)
- returned pointers are 16-byte aligned; usable capacity ≥ requested and fully writable;
- live allocations never overlap and stay inside the arena;
- `allocate` **splits** an oversized block, and **refuses** to split when the remainder `< kMinBlock`;
- out-of-memory returns `nullptr` and leaves the heap intact; `allocate(0)` is valid and freeable;
- **coalescing**: freeing adjacent blocks merges them, and freeing *everything* returns the arena to a
  single free block of the original size (no leak, no fragmentation);
- a 4000-step randomized alloc/free stress run keeps `check_invariants()` true after every step, with
  per-allocation fill patterns proving no neighbour ever clobbers another.

All tests build with **AddressSanitizer + UBSan** on (Debug).

## Measure the real thing too
`bench/heap_probe.cpp` reproduces the Lesson 7.1 numbers (brk growth, 128 kB mmap threshold, chunk
stride, `malloc_usable_size`) with the *real* glibc malloc. Build and run it, then record what your
machine prints in `RESULTS.md`:
```
cmake -S . -B build && cmake --build build --target heap_probe
./build/heap_probe
```

Run the tests: `./run.sh` or `./run.sh <filter>` (e.g. `./run.sh coalesce`).
Then fill in `DECISIONS.md` — one sourced line per decision.
