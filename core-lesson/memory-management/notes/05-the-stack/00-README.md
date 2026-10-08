# Lesson 5 — The Stack and the Calling Convention

> Status: complete. Exercise: `../../exercises/05-the-stack/`.
>
> The stack is where automatic storage (Lesson 2.1) actually lives, where function calls are
> tracked, and where the oldest exploitation technique in the book — the stack buffer overflow —
> happens. It's also what your capstone must *walk* to produce a backtrace ("this block was
> allocated here"). This lesson opens the stack frame and the calling convention so you can read a
> crash, a disassembly, and a backtrace fluently.

Read in order:
1. `01-what-the-stack-is.md` — the stack region, the stack pointer, frames, LIFO, grows-down (measured).
2. `02-the-calling-convention.md` — the SysV AMD64 ABI: argument/return registers, caller/callee-saved,
   the frame layout, 16-byte alignment, the red zone.
3. `03-frames-and-unwinding.md` — the frame-pointer chain, `backtrace()`, DWARF CFI, how a debugger/
   your capstone walks frames to a stack trace.
4. `04-stack-hazards.md` — stack overflow (recursion, VLAs, big locals), `alloca`, canaries, use-after-scope.
5. `05-security-view.md` — the classic stack smash → return-address hijack; canaries/NX/ASLR/ROP; three hats.
6. `06-glossary.md`.

Then do the exercise: a **stack prober + a `backtrace` wrapper** — the frame-walking your capstone needs.

## The one-paragraph picture
The stack is a contiguous region near the top of the address space (Lesson 3.3) that grows toward
**lower** addresses. A register, the **stack pointer (RSP)**, marks its current top. Calling a
function pushes a **frame**: the return address, maybe a saved frame pointer, the callee's locals and
spilled registers — all by *subtracting* from RSP. Returning pops the frame by restoring RSP and
jumping to the saved return address. The **calling convention** (the ABI) fixes which registers carry
arguments and who must preserve what, so code compiled separately interoperates. Because the return
address sits in writable stack memory right above a function's local buffers, a buffer overflow can
overwrite it — the root of stack-smashing — which is why stacks carry **canaries**, are
**non-executable**, and are placed by **ASLR**.
