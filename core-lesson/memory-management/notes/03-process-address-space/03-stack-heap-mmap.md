# 3.3 — How the Heap, Stack, and mmap Region Grow

The segments aren't fixed-size boxes; three of them change at run time. Knowing *which way* each
grows and *what's between them* explains stack overflows, heap exhaustion, and `mmap` addresses.

## 1. The heap grows up

The heap sits just above `.bss` and grows toward **higher** addresses. Historically it's extended by
the `brk`/`sbrk` system call, which moves "the program break" — the top of the heap — upward
(Lesson 7 covers this; `malloc` calls it). In the §3.1 measurement the heap was at `0x17f74010`,
above the globals at `0x404xxx` and far below the stack **(derived from the measured addresses)**.

Large allocations don't use the heap at all: glibc `malloc` switches to `mmap` above ~128 KiB
(the alignment lesson measured this), placing them in the mmap region (§3). That's why a big
`malloc` address (`0x7b…`) looks nothing like a small one (`0x17f…`).

## 2. The stack grows down

The stack starts near the top of the user address space (`0x7ffe…` measured) and grows toward
**lower** addresses: each function call pushes a **frame** (locals, saved registers, the return
address) by subtracting from the stack pointer (Lesson 5 is all about this). So heap-up and
stack-down grow *toward each other*, with the vast middle holding libraries and `mmap`s.

The stack is finite — 8 MiB by default on Linux (`ulimit -s`). Exhaust it (unbounded recursion, a
huge local array) and you hit the **guard page** below it and get a fault. `[MEM §5.1]` names this:

> "A stack overflow occurs when a program writes data outside the bounds of the stack ... Common
> causes: **Infinite recursion** ... **Large local arrays or objects**."

That's a different event from a heap "overflow" (writing past a heap block, §2.3). Same word, two
mechanisms — keep them distinct.

## 3. The mmap region: libraries and anonymous maps

Between heap and stack live:
- **shared libraries** (`libc.so`, etc.), each mapped as several VMAs (§3.2),
- **anonymous `mmap`s** — memory the program asked the kernel for directly with `mmap(MAP_ANONYMOUS)`
  (the alignment lesson used this; large `malloc`s land here; thread stacks too),
- the **vdso/vvar** kernel pages.

`mmap` returns page-aligned addresses (alignment lesson Part 3 §6); you saw `0x7b…`/`0x7f…` values.
This region is where most interesting runtime memory actually lives in a big program.

## 4. The gaps are unmapped on purpose

Most of the 2⁴⁸-byte space is **not mapped**. Touching an unmapped address faults immediately — which
is a feature: it catches wild pointers. The `---p` guard VMAs (§3.2) are deliberately-unmapped slivers
placed between regions so an overflow runs into a fault instead of a neighbour. Your capstone uses
exactly this idea on purpose: `mmap` a block, `mprotect(PROT_NONE)` a guard page right after it, and
an overflow becomes a catchable SIGSEGV.

## 5. The zero page

Address `0` (and a low range around it) is never mapped, so dereferencing `nullptr` faults instead of
silently reading real data (Lesson 2.2). This is why a null-pointer bug is usually a clean crash and
not a silent corruption — the address space is arranged to make `0` special.

## Drills
1. Print `&local` in `main`, then again inside a function `main` calls, then inside a function *that*
   calls. Confirm the addresses **decrease** (stack grows down).
2. `malloc(10)` twice and print both addresses (increasing — heap up). Then `malloc(1<<20)` and note
   the address jumps to the `0x7f…` mmap region. Tie to the alignment lesson's mmap-threshold
   measurement.
3. Write a bounded-depth recursion that prints `&local` each level; estimate the per-frame size from
   the address delta, then compute how many frames fit in `ulimit -s`.
4. Dereference `(int*)0` under a normal build and note the clean SIGSEGV; explain using §5.

## My summary
