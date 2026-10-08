# Exercise 12 — Build `UniquePtr`, `SharedPtr`, `WeakPtr`

Implement the marked members until `./run.sh` prints `ALL TESTS PASSED`.
Notes: `../../notes/12-smart-pointers/`.

You build the three standard smart pointers from scratch — the direct payoff of Lesson 11's rule of
five, over Lesson 8's `new`/placement-new.

- **`UniquePtr<T>`** (`include/mm/unique_ptr.hpp`, notes 12.1): exclusive, move-only ownership.
  You implement the **move constructor** and **move assignment** (steal the pointer, null the source).
  The dtor, raw ctor, deleted copies, accessors, `reset`/`release`, and `make_unique` are provided.
- **`SharedPtr<T>`** (`include/mm/shared_ptr.hpp`, notes 12.2): shared ownership via a reference-counted
  control block. You implement the **copy ctor** (`++strong`), **move ctor** (steal), **copy
  assignment** (copy-and-swap), and **move assignment**. The control block (`detail::CbBase`) with its
  **atomic** count operations is provided — read it; its methods (`inc_strong`, `dec_strong`,
  `inc_weak`, `dec_weak`, `incref_if_nonzero`) are the vocabulary you call.
- **`WeakPtr<T>`** (same header, notes 12.3): a non-owning observer. You implement the
  **constructor from a `SharedPtr`** (`++weak`), **`expired()`**, and **`lock()`** (atomically promote
  to a `SharedPtr` if the object is still alive, via `incref_if_nonzero`). The weak copy/move members
  are provided.

## What the tests check (Lesson 12 invariants)
- **unique**: move-only (copy is a compile error — `static_assert`); `get`/`*`/`->`; move transfers
  ownership and empties the source; self-move safe; `release`/`reset`; destroyed exactly once.
- **shared**: `use_count` tracks owners (1→2→1); the object is destroyed exactly once by the **last**
  owner; move transfers without changing the count; copy-assign is self-safe.
- **weak**: a `weak_ptr` does **not** keep the object alive; `lock()` returns a usable `SharedPtr`
  while alive and an **empty** one once expired (`expired()` flips); `lock()` can keep an object alive
  while held; and a `shared_ptr` **cycle** is freed once one edge is a `weak_ptr`.

All under AddressSanitizer + UBSan (which also prove: freed exactly once, no leak, no use-after-free).

## Notes on the stub state
- Smart-pointer operations are `noexcept`, so the stubs can't use `Todo()` (throwing from `noexcept`
  calls `std::terminate`). Instead the stubs produce **empty / no-op** pointers, so the program runs
  and the tests **FAIL** (with clear `lhs/rhs`) until you implement them — this exercise signals via
  FAIL, not `[ TODO ]`.
- The provided control block uses the "**strong owners hold one weak reference**" idiom (`weak` starts
  at 1). Read its comments — notes 12.2 explains why that extra weak ref makes freeing the control
  block reentrancy-safe. You don't change it; you call its methods.

Run: `./run.sh` or `./run.sh <filter>` (e.g. `./run.sh weak`). Then fill in `DECISIONS.md`.
