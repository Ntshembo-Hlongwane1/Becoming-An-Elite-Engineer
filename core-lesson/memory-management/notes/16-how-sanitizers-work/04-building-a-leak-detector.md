# 16.4 — Building a mini leak detector

Now assemble §16.1–§16.3 into the thing you build: a leak detector that hooks allocation, records
every live block in an out-of-line side table, detects double-/invalid-free, and reports what's
outstanding. It is a hand-rolled LeakSanitizer-for-your-own-allocations.

## 1. The shape

```cpp
class LeakTracker {
    // out-of-line side table: live pointer -> {size, id}. FIXED storage -> never allocates (§16.1 §3).
    struct Slot { void* key; std::size_t size; std::uint64_t id; State st; };
    Slot slots_[N];                 // N fixed (power of two); open addressing
    std::size_t live_count_, live_bytes_, total_allocations_, invalid_frees_;
    std::uint64_t next_id_;
    std::mutex mu_;                 // thread-safe (the real tools are)
  public:
    void track(void* p, std::size_t size);   // insert on alloc
    bool untrack(void* p);                    // erase on free; false => double/invalid free
    std::size_t report(Leak* out, std::size_t cap) const;  // fill still-live; return #leaks
    // counters: live_count/live_bytes/total_allocations/invalid_frees
};
```

The **hooks** are Lesson 8's interception in miniature:

```cpp
void* tracked_malloc(std::size_t n) { void* p = std::malloc(n); if (p) tracker().track(p, n); return p; }
void  tracked_free(void* p)         { if (p && tracker().untrack(p)) std::free(p); }  // only free if we owned it
```

(A real whole-program version replaces global `operator new`/`delete`, Lesson 8.4 — but then it must
add reachability, §16.3 §2, to avoid false positives from the runtime. The explicit hooks keep the
exercise deterministic.)

## 2. The three operations, precisely

- **`track(p, size)`**: insert `p` into the table with its `size` and a fresh `id`; bump
  `live_count_`, `live_bytes_ += size`, `total_allocations_`. (If `p` is somehow already present, that's
  the allocator handing back a live address — don't double-count.)
- **`untrack(p)`**: find `p` in the table.
  - present → remove it, `--live_count_`, `live_bytes_ -= size`, return **true** (caller frees).
  - absent → it was never tracked or already freed: `++invalid_frees_`, return **false** (a
    **double-free** or **invalid-free**, §16.3 §3 — the caller must *not* free).
- **`report(out, cap)`**: scan the table; every still-present slot is an outstanding block (a leak if
  it's never going to be freed). Copy up to `cap` of them into `out` and return the count.

Counters give the cheap queries (`live_count`, `live_bytes`, `total_allocations`, `invalid_frees`) that
tests assert on; `report` gives the detailed list.

## 3. Why fixed storage, out of line

Two deliberate design choices straight from §16.1:
- **Fixed-capacity table (no dynamic allocation).** If the tracker inserted into a `std::map`, that
  `map` would call `operator new` → if `operator new` is the hooked allocator, infinite recursion
  (§16.1 §3). A fixed array sidesteps it entirely — the detector allocates *nothing* on the hot path.
  (Real sanitizers reserve their metadata region up front for the same reason.)
- **Out of line (separate from user data).** The table isn't adjacent to the user's buffers, so a
  buffer overflow in the program under test can't corrupt the detector's records (§16.1 §2). A
  detector must stay trustworthy under the bugs it hunts.

## 4. Extending toward the capstone (what you'd add next)
- **Allocation-site backtrace** (`backtrace(3)`, `[MAN-backtrace]`): store a few return addresses per
  block so a leak report says *where* it was allocated — the thing that makes ASan/LSan reports
  actionable (§16.1 §2).
- **Redzones / guard pages** around each block to catch overflow and UAF (ASan's §16.2 half) — Lesson
  17's hardening; combine with this leak table and you have your capstone detector.
- **Reachability** (§16.3 §2) if you make it whole-program and report at exit.

For this exercise, the leak table + double/invalid-free detection is the deliverable, and it's a
genuinely useful tool as-is.

## Drills
1. Implement `track`/`untrack`/`report` and the counters. Verify: allocate 10, free 3, `live_count==7`,
   `report` lists exactly those 7; then free the rest and confirm `live_count==0`.
2. Make `untrack` detect double-free (free the same pointer twice) and invalid-free (free a pointer the
   tracker never saw). Check `invalid_frees` counts both and that the real `free` is not called on them.
3. Why must the table be fixed-capacity rather than a `std::vector` that grows? Trace the recursion a
   growing container would cause if the hooks replaced global `operator new` (§3, Lesson 8.4 §3).
4. Add an allocation `id` and a 3-frame backtrace to each record (`backtrace(3)`). Print a leak report
   in LSan's style ("N bytes in M blocks, allocated at …"). What did the backtrace let you say that the
   size alone could not?

## My summary
