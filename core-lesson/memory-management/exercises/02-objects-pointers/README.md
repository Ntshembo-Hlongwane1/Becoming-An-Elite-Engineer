# Exercise 2 — Objects, Pointers & Lifetime (a bounds-checked ByteCursor)

Implement `src/byte_cursor.cpp` until `./run.sh` prints `ALL TESTS PASSED`.
Notes: `../../notes/02-objects-pointers-lifetime/`.

Provided (do not edit): `src/codec.cpp` (your Lesson-1 little-endian codec), `include/mm/codec.hpp`.
The template methods `read_object`/`write_object` are already written in the header — study them as
the reference for aliasing-safe type punning (Lesson 2.4 §3); you implement the non-template methods.

Key rules (full contracts in `include/mm/byte_cursor.hpp`):
- Bounds-check with `n > remaining()`, never `position()+n > size()` (wraps — Lesson 1.3).
- A failed read returns `std::nullopt`, a failed write returns `false`, and **neither advances** the
  position (atomic failure).
- `read_bytes` returns a `std::span` view INTO the buffer (no copy); respect the lifetime rule
  (the view must not outlive the buffer — Lesson 2.5).
- ASan + UBSan are on. The capacity and object tests will trip any out-of-range write.

Run: `./run.sh` or `./run.sh <filter>`.
