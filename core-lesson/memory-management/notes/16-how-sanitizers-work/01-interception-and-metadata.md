# 16.1 — The two primitives: interception and metadata

Every memory tool — Valgrind, ASan, LSan, your capstone detector, your Lesson-16 exercise — is built
from two capabilities you already have. This file names them and the traps that make them harder than
they look.

## 1. Interception: see every allocation

To track memory, a tool must observe every `malloc`/`free` (and `new`/`delete`). Three ways, from
Lesson 8:
- **Replace the allocation functions.** Define your own global `operator new`/`operator delete`
  (Lesson 8.4) or override `malloc`/`free` (strong symbols that win over libc's). The whole program's
  allocations now flow through you, with **no change to application code** — `[MEM §13.1]` notes ASan
  is simply enabled with `-fsanitize=address` and then "run it as normal." ASan's runtime does exactly
  this: `[ASAN-ALGO]` "The run-time library replaces the `malloc` and `free` functions."
- **Preload a shim** (`LD_PRELOAD` a `.so` defining `malloc`/`free`) — same idea without recompiling.
- **Dynamic binary instrumentation** (Valgrind) — run the program on a synthetic CPU and intercept
  every memory operation. Most powerful, slowest (10–50×).

Your exercise uses the first: small wrappers (`tracked_malloc`/`tracked_free`) that call the real
allocator and record metadata — the explicit form of Lesson 8's `operator new` hook.

## 2. Metadata: remember something about each block

Interception alone counts; to *detect* you must remember. The two places to keep per-allocation
metadata (Lesson 7.2 taught the trade-off):
- **Inline header** (Lesson 7/8): store size/flags just before the returned pointer. Fast O(1) lookup
  from the pointer, but the metadata sits *next to* the user's data — so a buffer overflow corrupts it
  (Lesson 7.5), and that's bad for a *detector* that must stay trustworthy under the very bugs it
  hunts.
- **Out-of-line side table** (this lesson): a separate map `pointer → {size, id, allocation site}`.
  The user's overflow can't reach it, so the detector's records stay intact. ASan uses an out-of-line
  scheme (shadow memory, §16.2); your leak detector uses a side hash table. The capstone detector
  likewise keeps metadata out of line, with guard pages (Lesson 4.4) between it and user data.

What you record decides what you can report:
- size + liveness → **leak detection** and **double-free** (this exercise).
- + a **backtrace** of the allocation site (`backtrace(3)`, `[MAN-backtrace]`) → "leaked block was
  allocated *here*" (what makes ASan/LSan reports actionable).
- + redzone state per byte → **overflow / use-after-free** (ASan, §16.2; hardening, Lesson 17).

## 3. The two traps a real interceptor must dodge

Hooking the allocator is subtle precisely because your hook runs *inside* the allocation path:

1. **Recursion.** If your `track()` (called from the hooked allocator) itself allocates — e.g. inserts
   into a `std::map`/`std::vector` that calls `operator new` — it re-enters your hook → infinite
   recursion on the first allocation. (You met this in Lesson 8.4 §3.) The fix: the detector's own
   storage must **not** go through the hooked allocator — use **fixed, pre-reserved storage** (a static
   array / `mmap`), or call the *real* `malloc` directly, or a re-entrancy guard flag. Your exercise
   uses a fixed-capacity side table (no dynamic allocation at all).
2. **Early and late calls.** Allocations happen during static initialization (**before `main`**) and
   destruction (**after `main`**) — the runtime, iostreams, the test registry. The detector's state
   must be ready with **no dynamic initialization of its own** (constant-initialised, Lesson 8.4 §3),
   and robust to being called while it's being torn down. (A real leak report runs at the very end,
   via an `atexit`/destructor, after user code — which raises the reachability question of §16.3.)

These two constraints — fixed storage, no self-allocation, ready-before-main — are *why* sanitizer
runtimes are written the careful way they are, and they shape your exercise's design (§16.4).

## Drills
1. Why does using a `std::unordered_map<void*,size_t>` *inside* a `malloc` hook deadlock/recurse, while
   a fixed `std::array`-based table does not? (§3.1) What third option avoids recursion without a fixed
   cap?
2. Compare inline-header vs out-of-line metadata for a *detector* specifically: why does a tool that
   hunts buffer overflows prefer its metadata out of line? (§2, Lesson 7.5)
3. A real leak detector reports at program exit. Name two allocations that happen *after* `main`
   returns and explain why a naïve "report everything still live" over-reports (preview §16.3).
4. Which single extra piece of metadata turns "you leaked 40 bytes" into "you leaked 40 bytes
   allocated at file.cpp:12"? How would you capture it (§2, `[MAN-backtrace]`)?

## My summary
