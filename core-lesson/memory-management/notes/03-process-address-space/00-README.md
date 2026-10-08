# Lesson 3 — A Process's Address Space

> Status: complete. Exercise: `../../exercises/03-address-space/`.
>
> Lessons 1–2 lived inside one object. Now zoom out: *where in the process does that object live?*
> Every pointer value you've printed is an index into one giant per-process array of bytes — the
> **address space** — and that array has a structure: code here, globals there, heap growing up,
> stack growing down, libraries and mappings in the middle. Knowing the map is the difference
> between "segfault, no idea" and "that address is in the stack region, read-only, so this is a
> write through a bad pointer." Your capstone's detector classifies addresses for a living; this
> lesson is where you learn to read the map it works against.

Read in order:
1. `01-the-segments.md` — text/rodata/data/bss, heap, stack, mmap region; measured addresses.
2. `02-proc-maps.md` — `/proc/self/maps` field by field; permissions; pseudo-paths; what each VMA is.
3. `03-stack-heap-mmap.md` — how the heap and stack grow, the `mmap` region, guard/unmapped gaps.
4. `04-aslr-and-permissions.md` — ASLR (PIE vs `-no-pie`, measured), W^X / NX, why the zero page faults.
5. `05-security-view.md` — the map as the attacker's board: what each region buys an attacker; three hats.
6. `06-glossary.md`.

Then do the exercise: **a `/proc/self/maps` parser** that classifies any address into its region.

## The one-paragraph picture
A process sees a single flat array of virtual bytes from `0` to `2⁴⁸−1` (on x86-64). The kernel
fills parts of it with **mappings** (VMAs): the program's **code** (`.text`, read+execute), its
**read-only data** (`.rodata`), its **initialized** (`.data`) and **zero-initialized** (`.bss`)
globals, a **heap** that grows toward higher addresses, shared **libraries** and anonymous
**`mmap`** regions in the middle, and a **stack** that grows toward lower addresses from near the
top. Everything else is unmapped, so touching it faults. `/proc/self/maps` is the kernel showing you
this map, one line per mapping, which is exactly what the next lesson (virtual memory) implements and
what your capstone reads to say "this pointer points *there*."
