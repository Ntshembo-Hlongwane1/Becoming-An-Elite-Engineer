# 3.4 — ASLR, PIE, and Memory Permissions

Two defences are baked into the modern address space: the regions are placed at **random** addresses
each run (ASLR), and no region is both **writable and executable** (W^X / NX). Both are measurable,
and both shape how memory bugs become — or fail to become — exploits.

## 1. ASLR, measured

**Address Space Layout Randomization** places the stack, heap, libraries, and (for PIE binaries) the
program itself at different base addresses every execution, so an attacker can't hard-code where code
or data will be.

A **PIE** (Position-Independent Executable, the default on your toolchain) can be loaded anywhere; a
`-no-pie` binary is linked to fixed addresses. **(measured)** — same program, consecutive PIE runs:

```
run 1:  code &func = 0x5ae87d3911a9
run 2:  code &func = 0x5ee75cd691a9      ← different base every run (ASLR)
```

vs `-no-pie` (§3.1), where `&func` is always `0x401196`. So:
- The **low bits** are stable (page offset, alignment lesson Part 7 §7 — ASLR moves whole pages), the
  **high bits** randomize.
- An address leaked from one run tells you nothing about the next run's layout — unless a bug *also*
  leaks a current address (an **info leak**), which is why real exploits chain a leak with the bug.

Check/142 toggle on Linux: `/proc/sys/kernel/randomize_va_space` (0 off, 2 full); `setarch -R` runs
a program with ASLR off for debugging. Build PIE (default) for ASLR to apply to your own code.

## 2. W^X / NX, measured

Modern CPUs have a per-page **NX** ("no-execute") bit, so the kernel can map data pages non-executable
and code pages non-writable. The invariant: **no region is both writable and executable.**
**(measured)** on your VM:

```
grep -E '^\S+ rwx' /proc/self/maps   →   none (W^X enforced)
```

Consequences:
- The **stack** and **heap** are `rw-` (no x): you cannot execute bytes you wrote there. Classic
  "spray shellcode on the stack and jump to it" is dead.
- **Code** is `r-x` (no w): you cannot patch your own `.text` at run time (without `mprotect`).
- Attackers respond with **code reuse**: Return-Oriented Programming chains snippets ("gadgets") of
  *existing* executable code, needing no writable+executable page. That's why NX raised the bar but
  didn't end exploitation — Lesson 17.

To *deliberately* make memory executable (a JIT, say) you call `mprotect(PROT_READ|PROT_EXEC)` after
writing — and you flip it from writable to executable, never both at once if you care about W^X. Your
capstone uses `mprotect` the other direction: to make a freed/guard page `PROT_NONE` so touching it
faults.

## 3. Why permissions are per-page

Permissions are a property of the **page table** entries (Lesson 4), so they're enforced in hardware
at 4096-byte granularity, for free, on every access. The `maps` `perms` field is just the kernel
reporting those page-table bits for each VMA. That's why W^X and the guard pages cost nothing at run
time: the MMU checks them as part of the address translation it already does.

## 4. How these interact with your bugs
- A **wild write** to an unmapped or `r--`/`---` address faults immediately (good: loud crash).
- A **wild write** to a `rw-` region you didn't mean to touch silently corrupts (bad: needs ASan /
  your capstone to catch).
- ASLR means a **use-after-free or overflow** usually isn't *directly* exploitable without also
  leaking an address; your detector's job is to catch the memory error itself, before any of that.

## Drills
1. Run a PIE program 3 times printing `&main`; then `setarch -R ./prog` 3 times. Explain the
   difference. Then build `-no-pie` and confirm the address is now constant.
2. `grep rwx /proc/self/maps` across several running programs. Find any `rwx` region (a JIT like a
   browser or the JVM may have one) and explain why it's a bigger attack surface.
3. `mmap` a page, write a tiny function's bytes into it, `mprotect` it `PROT_READ|PROT_EXEC`, and call
   it. Then try to call it *without* the `mprotect` and observe the fault. Which bit changed?
4. Relate the "low bits stable, high bits random" fact to the alignment lesson's partial-pointer-
   overwrite note (Part 7 §7).

## My summary
