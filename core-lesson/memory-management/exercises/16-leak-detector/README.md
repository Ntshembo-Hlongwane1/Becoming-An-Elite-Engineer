# Exercise 16 — A mini leak detector (hooking new/malloc)

Implement `track`, `untrack`, and `report` in `include/mm/leak_tracker.hpp` until `./run.sh` prints
`ALL TESTS PASSED`. Notes: `../../notes/16-how-sanitizers-work/`.

A hand-rolled LeakSanitizer for your own allocations: the `tracked_malloc`/`tracked_free` hooks
(provided) route every allocation through a `LeakTracker` that keeps an **out-of-line, fixed-capacity
side table** mapping each live pointer → `{size, id}` (Lesson 16.1/16.4). Whatever is still tracked is
**outstanding** (a leak); untracking a pointer that isn't present is a **double-/invalid-free**.

## What you implement (the detector logic)
- **`track(p, size)`** — insert `p` into the table with its size and a fresh id; update
  `live_count_`, `live_bytes_`, `total_allocations_`.
- **`untrack(p)`** — if `p` is live, remove it, update counters, return `true` (caller frees); if not,
  `++invalid_frees_` and return `false` (a double-/invalid-free — caller must **not** free).
- **`report(out, cap)`** — copy up to `cap` of the still-live blocks into `out[]`; return the total
  number live.

Provided: the fixed open-addressing table with linear probing + tombstones (`find_slot`,
`slot_for_insert`, `hash`), the mutex, the counters, `reset`, the global `tracker()`, and the
`tracked_malloc`/`tracked_free` hooks. Read them — the comments spell out the contract.

## What the tests check (Lesson 16 invariants)
- live count and live bytes track allocations; `total_allocations` never decreases;
- freeing some blocks leaves the rest outstanding, and `report` lists exactly those;
- **double-free** (free the same pointer twice) and **invalid-free** (free a never-tracked pointer) are
  detected (`invalid_frees` counts them) and the real `free` is **not** called on them;
- `report` truncates to `cap` but returns the true total; a freed address reused by a later allocation
  is tracked as one fresh live block.

All under AddressSanitizer + UBSan.

## Note on the stub state
Because the stub `track`/`untrack` are unimplemented (`Todo`), the hooks allocate real memory that
never gets recorded or freed — so in the stub state **AddressSanitizer's own LeakSanitizer reports
those blocks as leaks**. That's fitting: you're building exactly what LSan does, and until you do, LSan
does it for you. Once your tracker is correct and the tests free what they allocate, the run is clean.

## Why fixed, out-of-line storage (Lesson 16.1 §3)
The table is a fixed static array, not a growing container, so the detector **allocates nothing** on
the hot path — a growing container would call the allocator the detector hooks and recurse. And it's
separate from user data, so a buffer overflow in the program under test can't corrupt the detector's
records. These are the same constraints real sanitizer runtimes work under.

Run: `./run.sh` or `./run.sh <filter>`. Then fill in `DECISIONS.md`.
