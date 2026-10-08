# Lesson 7 — How `malloc` Works (brk/mmap, chunks, bins, split & coalesce)

> Status: complete. Exercise: `../../exercises/07-malloc/`.
>
> Lessons 1–6 answered *what memory is* — bits, objects, the address space, pages, the stack, caches.
> This lesson answers *who hands it out*. Every `new`, every `std::vector` growth, every `std::string`
> on the heap ends up in `malloc`, and `malloc` is just a C++-sized program that asks the kernel for
> big slabs of pages (Lesson 4) and then **sub-divides and recycles** them for you. Understanding it
> turns three mysteries into mechanisms: why `malloc(100)` actually reserves 104+ bytes, why freeing
> memory usually does *not* give it back to the OS, and why a one-byte heap overflow can hand an
> attacker the whole process. You will then build your own allocator with the same bones.

This is Phase B, Lesson 1 (allocation). It assumes Lessons 1–6 only.

## Ground rules recap
Every claim is quoted from a cited source, or marked **(derived)** (reasoning you can check) or
**(measured)** (a program I ran on your VM — GCC 15.2.0 / glibc 2.43 — shown so you can re-run it).
Sources for this lesson are listed in each file and consolidated in `../../SOURCES.md` additions at
the bottom of this README.

## Read in order
1. `01-the-heap-and-the-kernel.md` — what "the heap" really is; `brk`/`sbrk` grow it, `mmap` for big
   requests; the 128 kB threshold; measured on your VM (the break grew 5.27 MB for 4 MB of requests).
2. `02-chunks-and-boundary-tags.md` — the chunk: a header carrying the size (and 3 flag bits in its
   low bits), 16-byte alignment, the minimum size, the user pointer vs the chunk pointer, and the
   boundary-tag trick that makes `free` O(1). Measured: `malloc(24)` costs a 32-byte chunk.
3. `03-free-lists-bins-and-fit.md` — free lists; first-fit vs best-fit; splitting an oversized chunk;
   how glibc shards its free lists into fastbins / unsorted / small / large bins + the top chunk.
4. `04-coalescing-and-fragmentation.md` — merging adjacent free chunks via the `PREV_INUSE` bit;
   internal vs external fragmentation; why `free` keeps the memory (trim threshold); the wilderness.
5. `05-security-view.md` — the three hats: heap overflow / use-after-free / double-free as *metadata
   corruption*, how researchers find it, the honest value ceiling, and the defences (safe-linking,
   tcache keys, hardened allocators, ASan, and by-construction bounds).
6. `06-glossary.md`.

Then do the exercise: **your own free-list allocator (split + coalesce)** — a fixed arena you carve
into boundary-tagged chunks, with a first-fit search, splitting on allocate, and neighbour
coalescing on free. The tests assert the invariants this lesson teaches (alignment, no overlap,
split leaves a usable remainder, freeing everything merges back to one chunk) and run under
AddressSanitizer + UBSan.

## The one-paragraph picture
`malloc` is a **user-space** memory manager that sits between your program and the kernel. It asks
the kernel for memory in big, page-aligned slabs — by moving the *program break* up with `brk`/`sbrk`
for the ordinary heap, or by `mmap`ing a fresh region for any single request ≥ 128 kB `[MAN-malloc]`.
It then cuts those slabs into **chunks**: each chunk carries a small **header** with its size and a
few status bits, so `malloc` can find a free chunk big enough, **split** off the part you asked for,
and hand you a pointer just past the header. On `free`, it flips the chunk to "free", links it into a
**free list**, and **coalesces** it with any adjacent free neighbours into one bigger chunk — using
size fields stored at chunk boundaries (**boundary tags**) so it can find the neighbours in O(1)
`[LEA]`. It rarely returns the slab to the kernel; it keeps it to satisfy your next request fast.
Because your data and `malloc`'s bookkeeping live side by side in the same slab, overflowing your
data overwrites `malloc`'s metadata — which is the root of an entire class of exploits.

## New sources introduced here (added to `SOURCES.md`)
| Key | Source |
|---|---|
| `[MAN-malloc]` | `malloc(3)` — heap via `sbrk`, `mmap` above `MMAP_THRESHOLD`=128 kB, alignment, `free(NULL)`, `malloc(0)`, double-free UB. https://man7.org/linux/man-pages/man3/malloc.3.html |
| `[MAN-sbrk]` | `brk(2)`/`sbrk(2)` — the program break; raising it allocates memory; `sbrk(0)` reads it. https://man7.org/linux/man-pages/man2/sbrk.2.html |
| `[MAN-mallopt]` | `mallopt(3)` — `M_MMAP_THRESHOLD` (128 kB), `M_TRIM_THRESHOLD` (128 kB), `M_MXFAST` (fastbin cap). https://man7.org/linux/man-pages/man3/mallopt.3.html |
| `[LEA]` | D. Lea, *A Memory Allocator* (dlmalloc) — boundary tags, bins, best-fit, splitting, coalescing, wilderness. https://gee.cs.oswego.edu/dl/html/malloc.html |
| `[GLIBC-malloc.c]` | glibc `malloc/malloc.c` — `struct malloc_chunk`, `PREV_INUSE/IS_MMAPPED/NON_MAIN_ARENA`, min size/alignment. https://sourceware.org/git/?p=glibc.git;a=blob;f=malloc/malloc.c |
| `[SAFELINK]` | glibc 2.32 "Safe-Linking" of singly-linked free lists (fastbins/tcache). https://sourceware.org/git/?p=glibc.git;a=commit;h=a1a486d70ebcc47a686ff5846875eacad0940e41 |
