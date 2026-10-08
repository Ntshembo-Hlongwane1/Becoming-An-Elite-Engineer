# Exercise 13 — A `SmallVector<T,N>` with inline storage

Implement the six marked members in `include/mm/small_vector.hpp` until `./run.sh` prints
`ALL TESTS PASSED`. Notes: `../../notes/13-container-memory/`.

`SmallVector<T,N>` is a vector with **N elements of inline storage** (kept inside the object, no heap)
that **spills to the heap** when it grows past N — the container LLVM/Chromium/engines use everywhere.
It combines Lesson 11 (rule of five, `move_if_noexcept`, strong guarantee) with Lesson 13.2's inline
buffer. Provided: the layout, `is_inline()`, raw alloc/free, accessors, `clear`, `push_back`, and the
destructor. You implement:

1. **`reserve`** — allocate a heap buffer, relocate with `std::move_if_noexcept` (strong guarantee),
   free the old buffer **only if it was heap** (`if (!is_inline())`).
2. **`emplace_back`** — grow (spill) if full, placement-new at the end.
3. **copy constructor** — deep copy.
4. **move constructor** — *the point of the exercise*: if the source is **inline**, MOVE its elements
   into your own inline buffer; if the source is on the **heap**, steal the buffer pointer (Lesson
   13.4 §3). You cannot steal a pointer from an inline source — it aims into the source object itself.
5. **copy assignment** — copy-and-move (strong guarantee + self-safe).
6. **move assignment** — release what you hold, then the same inline-relocate / heap-steal split.

## What the tests check (Lesson 13 invariants)
- fresh is inline, capacity N, data inside the object; stays inline up to N, then **spills to heap**
  (data leaves the object) with elements preserved; values survive many spills/growths; `.at()` throws;
- **move from an inline source relocates** into the destination's own buffer (`b.data() != a.data()`,
  `b` is inline and points inside itself) — the critical correctness check;
- **move from a heap source steals** the exact buffer pointer (no element copy);
- copy is deep/independent; copy-assign is self-safe; move-assign handles inline and heap sources;
- RAII frees everything in both the inline and heap cases; move-only element types (`unique_ptr`)
  work across a spill.

All under AddressSanitizer + UBSan.

## Traps the tests target (Lesson 13.4)
- **Never `free` the inline buffer** — it came from the object, not `operator new`. Guard every
  `free_raw(data_)` with `if (!is_inline())`.
- **Move must not steal an inline pointer** — that would dangle into the moved-from object. Split on
  `is_inline()`.
- The move operations are **not** `noexcept` (the inline branch move-constructs `T`s, which may
  throw) — that's correct and matches real `SmallVector`s.

Run: `./run.sh` or `./run.sh <filter>` (e.g. `./run.sh move`). Then fill in `DECISIONS.md`.
