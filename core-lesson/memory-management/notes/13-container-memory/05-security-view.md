# 13.5 — The Security Researcher's View: invalidation is a use-after-free factory

Lessons 11–12 covered ownership bugs (wrong rule of five, escaped `.get()`). This lesson's bug class
is quieter and, by volume, one of the **most common memory-safety bugs in real C++**: a pointer,
reference, or iterator held across a container mutation that **invalidates** it (§13.3). It is a
textbook use-after-free (Lesson 7.5), but the code looks innocent — no `delete`, no `free`, just a
`push_back`. Three hats.

## The bug class
- **Dangling after reallocation.** Hold `&v[i]` / `v.begin()` / `v.data()`, then `push_back` (or
  `insert`/`resize`/`reserve`) grows past capacity → the old buffer is freed, the handle dangles →
  read/write is a UAF (§13.3 §3–4). Also `SmallVector`'s inline→heap **spill** and a **move** of the
  container.
- **Erase/iterate dangling.** Using an iterator after `erase` (or after the element it named was
  removed) → UAF / wrong element.
- **Self-referential insert.** `v.push_back(v[0])` / `v.insert(it, v.back())` where the argument aliases
  an element that the reallocation frees mid-call.
- **OOB on a container.** `operator[]`/`data()[i]` with an out-of-range `i` (no bounds check) →
  heap-buffer-overflow; an attacker-controlled index or size (Lesson 1 integer bug feeding it) turns
  it into arbitrary read/write.
- **SBO-specific:** stealing an inline pointer in a hand-written move (§13.4 §3) → dangling into a dead
  object; or assuming `data()` is stable across a move/spill.

## Offense — discovery
- **Source/RE:** the signature is a handle taken from a container and **used after a mutation of that
  container** between the two points. Grep for `&vec[`, `vec.data()`, `vec.begin()`/`.end()`, a
  reference or pointer *member* caching a container element, inside or before a loop that
  `push_back`/`insert`/`erase`/`resize`s the same container. `operator[]` with a non-constant index
  near untrusted input is the OOB lead.
- **Dynamic:** ASan catches it precisely — `heap-use-after-free` on the dangling access with the
  *free* stack pointing at the reallocation inside `push_back`/`insert`, and `heap-buffer-overflow`
  for the OOB. Fuzzing container-manipulating code under ASan surfaces these fast. This is a very
  common finding in real audits.

## Offense — value, honest ceiling
- **Ceiling:** it's a genuine UAF/OOB — the Lesson-7.5 primitives. A dangling reference you can still
  *write* through, after the freed buffer is reallocated to attacker-controlled data, is a
  write-what-where → up to code execution; an OOB with a controlled index is a direct read/write
  primitive. These are the real bugs behind many browser/engine CVEs.
- **Floor / obstacles:** the window is often small (the reallocation must happen *between* taking the
  handle and using it), and the freed buffer must be groomed back under attacker control (Lesson 7.5),
  plus allocator hardening and ASLR. The honest report names the exact handle, the mutating call that
  invalidates it, and the grooming still required.

## Defense — and what removes the class
- **Don't hold a handle across a mutation.** Re-fetch `v[i]` *after* the `push_back`/`insert`, or index
  by position (`std::size_t i`) rather than by pointer/iterator. This single habit kills most of the
  class. (§13.3 §4.)
- **`reserve` up front** when the final size is known: no reallocation means no invalidation during the
  fill (§13.1 §4) — a correctness tool, not just a speed one.
- **Prefer indices or stable-reference containers** when you *must* keep references across mutation:
  `std::list`/`map`/node-based (references never invalidate on insert, §13.3 §5), or store handles as
  indices into the vector.
- **Bounds-check untrusted indices**: `.at()` (throws) or an explicit check; never feed an
  unchecked attacker value to `operator[]` (ties to Lesson 1's length discipline).
- **Use `erase`'s return value** for erase-while-iterating; use range-for only when not mutating.
- **For hand-written SBO/containers (this exercise):** get the move's inline/heap split right (§13.4
  §3) and never `free` the inline buffer; test the moved-from and the spill boundary explicitly.
- **Tooling:** ASan+UBSan in CI (every exercise here), plus clang-tidy / `-Wdangling` and the
  lifetime-analysis warnings that flag some held-across-mutation cases at compile time.

## Through-line to the capstone
Your heap detector flags exactly these as `heap-use-after-free` / `heap-buffer-overflow`; this lesson
explains the *innocent-looking source* — a `push_back` in a loop — so you fix the pattern (re-fetch /
`reserve` / index), not just silence the tool. And when you reverse a target that uses inline/SBO
containers (every engine and browser does), the move-relocation and spill boundaries are where
lifetime bugs hide — the same seams you just learned to build correctly.

## My summary
