# Exercise 5 — The Stack (a prober + a backtrace wrapper)

Implement `src/stack.cpp` until `./run.sh` prints `ALL TESTS PASSED`.
Notes: `../../notes/05-the-stack/`.

You build the two things your capstone needs from the stack: reasoning about frame layout, and
walking the stack into a backtrace (Lesson 5.3) to report "allocated here".

Functions (contracts in `include/mm/stack.hpp`):
- `stack_direction()` — prove grow-down by comparing nested locals (Lesson 5.1 §3).
- `stack_limit_bytes()` — `getrlimit(RLIMIT_STACK)` (Lesson 5.1 §4).
- `adjacent_frame_delta()` — signed inner−outer local distance (negative on x86-64).
- `capture_backtrace(span)` — wrap glibc `backtrace()` (Lesson 5.3 §4).

The build adds `-rdynamic` and `-fno-omit-frame-pointer`. ASan+UBSan are on.

Run: `./run.sh` or `./run.sh <filter>`.
