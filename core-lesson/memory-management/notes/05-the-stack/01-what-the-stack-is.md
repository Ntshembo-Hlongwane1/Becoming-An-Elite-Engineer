# 5.1 — What the Stack Is

## 1. One region, one pointer, LIFO discipline

The **stack** is a single contiguous region (a VMA tagged `[stack]`, Lesson 3.2–3.3) that the CPU
uses to track function calls and hold automatic locals. A dedicated register, the **stack pointer**
(`RSP` on x86-64), always points at the current **top** of the stack. "Push" means *decrement* RSP
and store; "pop" means load and *increment* RSP — because the stack grows **downward** (toward lower
addresses, Lesson 3.3).

It's **LIFO** (last-in, first-out): the most recently called function is at the top, and it must
return before its caller resumes. That nesting is exactly the nesting of calls, which is why the
stack *is* the call history (§5.3 turns that into a backtrace).

## 2. A frame per call

Each function call gets a **stack frame**: a slice of the stack holding
- the **return address** (where to resume in the caller),
- optionally a saved **frame pointer** (`RBP`) linking to the caller's frame,
- the function's **local variables** (automatic storage, Lesson 2.1),
- space to **spill** registers and to pass extra arguments.

Entering a function *subtracts* its frame size from RSP (allocating the frame); returning *adds* it
back (freeing it). That's why automatic storage is "free" to allocate — it's one subtraction — and
why a local's lifetime ends at the closing brace: its frame is gone (Lesson 2.5's use-after-scope).

## 3. Measured: frames descend

This program prints the address of a local in `main` and in three nested calls **(measured)**:

```
main   frame≈0x7ffdee3600cc
a      frame≈0x7ffdee3600a4      ← lower than main
b      frame≈0x7ffdee360084      ← lower than a
c      frame≈0x7ffdee360064      ← lower than b
builtin_frame_address(0)=0x7ffdee3600e0
```

Each deeper call's local sits at a **lower** address (the stack grew down into it), by ~0x20 bytes
here — this frame holds just an `int` plus saved registers and alignment padding. Reverse the
reasoning and you can *read depth from addresses*: a smaller `[stack]` address is a deeper call. The
addresses are all `0x7ffd…`, the top-of-space stack region from Lesson 3.3.

## 4. The stack is finite

Unlike the heap, the stack has a hard size limit — **8 MiB** on your VM **(measured, `ulimit -s`
= 8192 kB)** — set by `RLIMIT_STACK` (`getrlimit(RLIMIT_STACK)`). Below the stack is a **guard page**
(Lesson 3.3/4.5); growing into it faults. So:
- deep/unbounded **recursion** exhausts the stack (§5.4),
- a huge **local array** (`char buf[10'000'000]`) can blow it in one call,
- each **thread** gets its own stack (a separate mapping, often smaller), sized at thread creation.

The exercise reads this limit with `getrlimit` and reasons about how many frames fit.

## 5. Why locals are fast (and dangerous)

Allocation is a register subtraction (no allocator, no locking, perfect locality — the frame is
usually hot in cache, Lesson 6). That speed is why you prefer stack locals to `new`. The danger is
the flip side of §2: the return address and saved registers live in the *same writable region*,
just above the locals, so an overflow of a local buffer walks straight into them (§5.5). Fast and
unprotected is a combination attackers love.

## Drills
1. Reproduce §3. Add a second local of a big type (`double d[8]`) to each function and watch the
   per-frame stride grow. Explain the delta from the sizes.
2. Print `getrlimit(RLIMIT_STACK)` and confirm it matches `ulimit -s` × 1024. Change it with
   `ulimit -s 1024` in the shell and re-run.
3. Write a function returning `&local` and compare its address to a local in `main` called after it.
   Why can the second call's local reuse the same address? (Lesson 2.5.)
4. Predict the sign of `(&inner_local) - (&outer_local)` for a nested call on x86-64; verify.

## My summary
