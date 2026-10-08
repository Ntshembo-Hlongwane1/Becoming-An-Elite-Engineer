# 7.1 — The heap, and where it comes from

## 1. "The heap" is a region, not a data structure

From Lesson 3 you already have the picture: a process's address space has segments, and one of them
is the **heap** — the region that grows at runtime for dynamic allocation. Your book names it plainly:

> "The heap is used for dynamic memory allocation at runtime. It provides flexibility but requires
> explicit management by developers to avoid leaks and fragmentation." `[MEM §1.2.3]`

Two warnings hide in that sentence, and this whole lesson unpacks them:
- **"flexibility"** — you can ask for any size at any time, unlike the stack (Lesson 5) whose size is
  fixed by the calling convention. That flexibility is exactly what `malloc` implements.
- **"fragmentation"** — because requests come and go in any order, the free space can end up chopped
  into pieces too small to use even when the *total* free space is large (§7.4).

Be careful with the word "heap". It is **not** the binary-heap data structure from Lesson-era DSA;
here it is a *memory region*. In glibc's own source the chunk-managing region is also called an
"arena", and the term "heap" is reused again for the per-arena slab `[GLIBC-malloc.c]`. Context tells
you which. In this file "the heap" = the classic brk-grown region of your process.

## 2. `malloc` is a program, running in your process

The key mental shift: **`malloc` is not a system call.** It is ordinary C code, compiled into libc,
running in *your* process at *your* privilege level. It keeps its own data structures (free lists,
size tables) in your address space. It only talks to the kernel occasionally — when it needs *more*
raw memory, or wants to give some back. `malloc(3)` says exactly this:

> "Normally, `malloc()` allocates memory from the heap, and adjusts the size of the heap as required,
> using `sbrk(2)`. When allocating blocks of memory larger than `MMAP_THRESHOLD` bytes, the glibc
> `malloc()` implementation allocates the memory as a private anonymous mapping using `mmap(2)`.
> `MMAP_THRESHOLD` is 128 kB by default." `[MAN-malloc]`

So there are **two** underlying sources of raw memory, and `malloc` picks between them by size:
1. **The program break** (`brk`/`sbrk`) — for ordinary, smaller requests. §3.
2. **A fresh `mmap`** (Lesson 4.4) — for any single request ≥ 128 kB. §4.

## 3. The program break: `brk` and `sbrk`

The **program break** is the current top of the heap — the first address past the heap's data.
`brk(2)`/`sbrk(2)` move it:

> "the program break is the first location after the end of the uninitialized data segment" and it
> "defines the end of the process's data segment." `[MAN-sbrk]`
>
> "Increasing the program break has the effect of allocating memory to the process; decreasing the
> break deallocates memory." `[MAN-sbrk]`
>
> "Calling `sbrk()` with an increment of 0 can be used to find the current location of the program
> break." `[MAN-sbrk]`

`sbrk(n)` adds `n` bytes to the heap and returns the *old* break (so `sbrk(0)` reads the break
without changing it); `brk(addr)` sets the break to an absolute address `[MAN-sbrk]`. Raising the
break does **not** touch physical RAM — it just enlarges the virtual mapping. The real pages are
faulted in lazily on first touch, exactly the demand-paging you measured in Lesson 4.2. That is why
`malloc` of a big region is cheap until you write to it.

The man page also tells you, bluntly, not to call these yourself:

> "Avoid using `brk()` and `sbrk()`: the `malloc(3)` memory allocation package is the portable and
> comfortable way of allocating memory." `[MAN-sbrk]`

Right — because raw `sbrk` gives you one giant stack-like region with no way to free a piece in the
middle. Turning that one knob into a general allocator is `malloc`'s whole job.

### (measured) the break actually moves
`sbrk(0)` before and after allocating ~4 MB in 64-byte pieces, on your VM:
```
break at start of main:                 0x64461ffdb000
break after ~4MB of small mallocs:      0x6446204e3000
the program break grew by 5275648 bytes (sbrk/brk moved the heap up)
```
Four MB of *requests* grew the break by **5.27 MB** — the extra ~1.27 MB is per-chunk overhead (§7.2)
plus the slack `malloc` grabs ahead of need so it needn't call the kernel on every request **(derived)**.
Source: `m2.cpp` (in the lesson scratch; reproduced in the exercise's `bench/` too).

### (measured) a small malloc need not move the break at all
```
program break (sbrk(0)) before:  0x57d7a9f7d000
after malloc(100): ptr=0x57d7a9f5c020  break=0x57d7a9f7d000  break moved by 0 bytes
```
The returned pointer (`...5c020`) is *below* the break, inside heap space glibc had **already** grown
during process start-up (before `main`). The lesson: `malloc` calls the kernel only when its current
slab can't satisfy you; most `malloc`s are pure user-space list surgery with no syscall at all
**(derived)**. Source: `m1.cpp`.

## 4. Big requests go straight to `mmap`

For a single request ≥ `M_MMAP_THRESHOLD` (128 kB default `[MAN-mallopt]`), glibc skips the break and
calls `mmap(2)` for a private, anonymous mapping of its own (Lesson 4.4). `mallopt(3)` states it:

> "For allocations greater than or equal to the limit specified (in bytes) by `M_MMAP_THRESHOLD` that
> can't be satisfied from the free list, the memory-allocation functions employ `mmap(2)` instead of
> increasing the program break using `sbrk(2)`." `[MAN-mallopt]`

Why a separate mechanism? A 1 MB block carved out of the middle of the brk-heap could never be
returned to the OS until everything above it was freed (the break only moves at the top). A block
that *is* its own `mmap` can be handed back immediately with `munmap` on `free` — no fragmentation of
the main heap, and the memory genuinely returns to the system. The cost is a syscall and a page-table
edit per allocation, which is why the threshold exists: small, frequent requests stay in user space.

### (measured) the big one comes from a different address range
```
malloc(24)     x=0x57d7a9f5c090   (heap, just below the break)
malloc(200000) big=0x75e5eddcf010 break moved by 0  (0 => served by mmap)
distance big - small x = 33046617599872 bytes (mmap region is far from the heap)
```
`malloc(24)` sits in the brk-heap near `0x57d7...`; `malloc(200000)` lands in a wholly different
region near `0x75e5...`, ~30 TB away in virtual-address terms, and the break didn't move — because it
is an independent `mmap`, placed by the kernel wherever the mmap area lives (Lesson 3.3). Source:
`m1.cpp`.

## 5. Two edge cases the standard pins down
These bite beginners and show up in the exercise tests, so learn them from the spec, not from habit:
- **`free(NULL)` is a no-op.** "If `p` is NULL, no operation is performed." `[MAN-malloc]` So
  `free(ptr)` after `ptr = NULL` is always safe — no need to guard it.
- **`malloc(0)` returns something freeable.** "If size is 0, then `malloc()` returns a unique pointer
  value that can later be successfully passed to `free()`." `[MAN-malloc]` It may return a tiny real
  chunk or a special pointer; either way you may `free` it, and you must not dereference it.
  ((measured) on your VM it returned a normal-looking heap pointer, `0x...c0b0`.)
- **Mixing families is undefined.** Memory from `malloc` must go to `free`; from `new` to `delete`;
  from `new[]` to `delete[]`. `[MEM §2.3]`: "memory allocated with `new` should be released with
  `delete`, and memory allocated with `malloc` should be freed with `free`." We return to the `new`
  side in Lesson 8.

## 6. Where this leaves us
`malloc` owns one or more big page-slabs obtained from the kernel and never wants to go back to the
kernel if it can avoid it. Everything from here — chunks, headers, bins, coalescing — is how it
sub-divides and recycles those slabs entirely in user space. Next: the unit it cuts them into.

## Drills
1. Re-run `m1.cpp`/`m2.cpp` yourself. Change the small-alloc size in `m2` from 64 to 4096; does the
   break grow by more or less *per request*? Explain via overhead vs payload (§7.2 will confirm).
2. Call `malloc(200000)` then `free` it, then `malloc(200000)` again, printing the pointer each time.
   Is it the same address? (Hint: `mmap`/`munmap` vs keeping it on a free list — relate to §4.)
3. From `/proc/self/maps` (Lesson 3.2), find the `[heap]` line before and after allocating 10 MB in
   small pieces. Which boundary of `[heap]` moves — low or high? Why only that one? (§3)
4. Predict: does raising `M_MMAP_THRESHOLD` with `mallopt` to 1 MB make a `malloc(200000)` come from
   the brk-heap instead of `mmap`? Try it and confirm with the address-range trick from §4.

## My summary
