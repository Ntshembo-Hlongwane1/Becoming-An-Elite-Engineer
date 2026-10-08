# 5.4 — Stack Hazards

The stack's speed comes from having no allocator and no bounds checks. The hazards all follow from
that plus its fixed size (§5.1 §4).

## 1. Stack overflow (exhaustion)
Run out of the ~8 MiB and you hit the guard page → fault/crash. `[MEM §5.1]`:

> "A stack overflow occurs when a program writes data outside the bounds of the stack ... Common
> causes: **Infinite recursion** ... **Large local arrays or objects**."

**(measured)** — unbounded recursion with a 256-byte local per frame, under ASan:
```
ERROR: AddressSanitizer: stack-overflow on address 0x7ffe836dfff8 ... in r()
```
Without a sanitizer you get a plain SIGSEGV at the guard page. Causes and fixes:
- **Unbounded/too-deep recursion** → add a base case / depth cap, or convert to an explicit heap stack
  (iteration). Note: tail-call optimization may save you at `-O2` but is not guaranteed in C++.
- **Huge locals** (`char buf[8<<20]`, a big `std::array`) → put large buffers on the **heap**.
- **VLAs / `alloca`** (§2) → avoid for sizes you don't control.

This is distinct from a stack *buffer* overflow (§5.5) — exhaustion vs. overwriting within a frame.

## 2. alloca / VLAs — stack allocation with a runtime size
`alloca(n)` and C99 variable-length arrays grab `n` bytes from the current frame by moving RSP. They
are fast and auto-freed on return, but:
- an attacker-influenced `n` can **jump the stack pointer past the guard page** into other memory
  (a "stack clash") — a real exploitation class;
- they complicate frame layout and are easy to misuse.
Prefer a fixed cap + heap fallback. The compiler flag `-fstack-clash-protection` probes each page to
make the clash fault safely.

## 3. Use-after-scope (recap from Lesson 2.5)
Returning or stashing a pointer to a local is a dangling pointer: the frame (and thus the storage) is
gone on return. ASan's `stack-use-after-return` and `-Wreturn-local-addr` catch it. It's the stack
twin of use-after-free.

## 4. Uninitialized locals
Stack storage is **not** zeroed (unlike fresh anonymous pages, Lesson 4.2). A local you read before
writing has an indeterminate value — often leftover bytes from a previous call's frame, which is both
a bug and an **information leak** (stale data, possibly secrets, Lesson 2.4 / alignment lesson Part 7).
`-ftrivial-auto-var-init=zero` and MSan (`-fsanitize=memory`) address this.

## Drills
1. Reproduce the ASan stack-overflow. Then cap the recursion depth and show it completes. Estimate
   the per-frame size from `8 MiB / max_depth`.
2. Move a 4-MiB local array to the heap and show the overflow disappears.
3. Read an uninitialized local under MSan (`-fsanitize=memory`, clang) and capture the report; then
   initialize it.
4. Explain the difference between "stack overflow" (§1) and "stack buffer overflow" (§5.5) in one
   sentence each.

## My summary
