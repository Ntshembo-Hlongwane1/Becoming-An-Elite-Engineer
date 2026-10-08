# 5.2 — The Calling Convention (SysV AMD64 ABI)

Separately-compiled functions must agree on *how* a call works: which registers carry arguments,
where the return value goes, who preserves what. That contract is the **ABI** (Application Binary
Interface). On Linux x86-64 it's the **System V AMD64 ABI** `[SYSV-ABI]`. You don't write it, but you
must read it to follow a disassembly, a crash, or an exploit.

## 1. Where arguments and returns go
- The first **six integer/pointer arguments** go in registers, in order:
  `RDI, RSI, RDX, RCX, R8, R9`. Further arguments go on the **stack** (pushed by the caller).
  (Floating-point args use `XMM0–XMM7`.)
- The **return value** comes back in `RAX` (and `RDX` for 128-bit); FP in `XMM0`.

So `f(a, b, c)` puts `a` in RDI, `b` in RSI, `c` in RDX, then `call f`. Reading those registers at a
crash tells you the arguments — a core RE skill.

## 2. Who preserves what
- **Callee-saved** (the called function must restore them before returning):
  `RBX, RBP, R12–R15, RSP`. If a function uses them, it saves them in its frame and restores on exit.
- **Caller-saved / scratch** (`RAX, RCX, RDX, RSI, RDI, R8–R11`): the caller must save them *before*
  a call if it still needs them afterward, because the callee may clobber them.

This split is why a backtrace can be reconstructed: callee-saved `RBP` (when used as a frame pointer)
chains frames together (§5.3).

## 3. The frame layout at a call

When the caller executes `call f`, the CPU **pushes the return address** (the instruction after
`call`) onto the stack and jumps to `f`. A typical `f` prologue then saves the old frame pointer:

```
            higher addresses
   ...            caller's frame
   [ arg 7+ ]     stack-passed args (if any)
   [ return addr ]   <- pushed by `call`
   [ saved RBP ]     <- f's prologue: push rbp; mov rbp, rsp
   [ f's locals ]    <- f: sub rsp, N
   [ spilled regs ]
   ...            <- RSP (top)
            lower addresses
```

So within `f`, the **return address is at a fixed offset above the locals**. A buffer local that
overflows upward (toward higher addresses, as `strcpy` writes) marches: local buffer → saved RBP →
**return address** → caller's frame. That geometry is the stack-smash of §5.5, and it's visible right
here in the frame layout.

## 4. 16-byte alignment and the red zone
- **Alignment:** the ABI requires `RSP` to be 16-byte aligned at the point of a `call` `[SYSV-ABI]`.
  That's the rule behind the alignment lesson's "stack is 16-aligned at calls" and why a local with
  `alignas(64)` makes the compiler emit extra `and rsp, -64`.
- **Red zone:** the 128 bytes *below* RSP are reserved for the current function's scratch use without
  adjusting RSP — the "red zone." Leaf functions exploit it to skip a frame setup. (Signal handlers
  must avoid clobbering it; the kernel respects it.)

## 5. Frame pointer: optional but useful
Using `RBP` as a frame pointer (the `push rbp; mov rbp,rsp` prologue) makes every frame link to its
caller's, forming a chain (§5.3) that trivially supports backtraces. But it costs a register, so
optimizers often **omit the frame pointer** (`-fomit-frame-pointer`, on by default at `-O2`) and rely
on **DWARF CFI** (§5.3) to unwind instead. Whether frame pointers exist changes how your capstone can
walk the stack — hence `backtrace()` / libunwind use CFI, not a naive RBP walk.

## Drills
1. Compile a small `int add3(int,int,int)` with `g++ -O0 -S` and find where the three args land
   (RDI/RSI/RDX) and where the return goes (RAX). Then the prologue (`push rbp; mov rbp,rsp; sub rsp`).
2. Build with `-fno-omit-frame-pointer` vs `-fomit-frame-pointer -O2` and diff the prologues.
3. Find, in a disassembly, the instruction that enforces 16-byte alignment for a function with an
   `alignas(32)` local.
4. Using §3's layout, draw where a 16-byte `char buf[16]` sits relative to the return address, and
   how many bytes of overflow reach it.

## My summary
