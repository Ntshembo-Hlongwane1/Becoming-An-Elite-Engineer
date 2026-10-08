# Exercise 8 — An allocation-counting global `operator new`

Implement the two core operators in `src/counting_new.cpp` until `./run.sh` prints `ALL TESTS PASSED`.
Notes: `../../notes/08-new-and-delete/` (especially `04-replacing-global-operator-new.md`).

You **replace the global `operator new` / `operator delete`** so that every `new` and `delete` in the
whole program is counted — live allocations, total allocations, total frees, live/total bytes, and a
peak high-water mark. This is the hook every leak detector and allocation profiler is built on
(Lesson 8.4–8.5), and the seed of the Lesson 16 detector.

## What you implement (just two functions)
In `src/counting_new.cpp`:
- `void* operator new(std::size_t n)` — allocate `kHeader + n` bytes with `std::malloc`, stash the
  size in the 16-byte `mm::detail::Header` (so `delete` can recover it — Lesson 8.4 §2), bump the
  counters, and return the pointer **after** the header. Throw `std::bad_alloc` if `malloc` fails.
- `void operator delete(void* p) noexcept` — `nullptr` is a no-op; otherwise read the header, update
  the counters (`live_allocations--`, `live_bytes -= size`, `total_frees++`), and `std::free` the
  **original** pointer.

Everything else is provided: the counters and the size-header helper in `include/mm/counting_new.hpp`,
and the rest of the operator family (`new[]`, `delete[]`, sized, nothrow) in
`src/counting_new_family.cpp`, which all delegate to your two.

## How this exercise differs from the others
A global `operator new` that threw `Todo()` would **abort the program before `main`** — the C++
runtime allocates through it during start-up (Lesson 8.4 §3). So the stubs are **functional
passthroughs**: the program runs from the start, and the counting tests simply **FAIL** (you'll see
`lhs: 0  rhs: 1`) until you add the bookkeeping. (While tests fail they skip their `delete` cleanup,
so you'll also see an AddressSanitizer leak dump — that disappears once the tests pass and clean up.)

Keep `new` and `delete` **consistent**: if `new` returns `raw + kHeader`, then `delete` must free
`user - kHeader`, or you'll hand `free` a pointer it never allocated.

## What the tests check (Lesson 8 invariants)
- a single `new`/`delete` moves `live_allocations` by exactly ±1;
- bytes are tracked for a known size (`new double` → +8), and return to baseline on delete;
- `total_allocations` counts every `new`; `new int[8]` is **one** allocation (≥ element bytes, allowing
  a cookie — Lesson 8.1 §4); `new (std::nothrow)` and a class with a ctor/dtor are each one allocation;
- `peak_bytes` is a high-water mark that does **not** fall when memory is freed;
- 10 outstanding `::operator new`s show as 10 live (a "leak" while held), then clean up;
- `delete nullptr` touches nothing.

All under AddressSanitizer + UBSan.

## Measure the real thing
`bench/newdelete_probe.cpp` reproduces the Lesson 8.1 measurements (default new alignment, the
allocate-before-construct order, and the array cookie) with its own logging operators:
```
cmake -S . -B build && cmake --build build --target newdelete_probe && ./build/newdelete_probe
```
Record what your machine prints in `RESULTS.md`.

Run the tests: `./run.sh` or `./run.sh <filter>`. Then fill in `DECISIONS.md`.
