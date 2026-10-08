# 7.3 — Free lists, bins, fit, and splitting

A chunk that you `free` isn't given back to the kernel (§7.1); it is recorded as *available* so the
next `malloc` can reuse it. The record is a **free list**. How `malloc` organises those lists and how
it picks a chunk to satisfy a request is the heart of allocator design — and the heart of the
exercise.

## 1. The free list

The simplest allocator keeps **one** doubly-linked list of all free chunks, threaded through the
`fd`/`bk` pointers that live inside each free chunk's payload (§7.2 §2). `malloc(n)`:
1. walks the list looking for a chunk big enough,
2. removes it from the list,
3. (if it's much bigger than `n`) **splits** it (§3), returning the remainder to the list,
4. marks it in use and returns the payload pointer.

`free(p)` pushes the chunk back onto the list and coalesces (§7.4). That single-list design is
exactly what the exercise builds — small enough to hold in your head, with every real-allocator idea
present. The rest of this file is *how the single list grows up into glibc's bins*, so you can read a
real heap later.

## 2. Which free chunk do you pick? Fit policies

When several free chunks fit, the choice is a **placement policy** with a classic trade-off:

- **First-fit** — take the first chunk on the list that is big enough. Fast (stop at the first hit),
  but tends to chip the chunks near the list head into small leftovers.
- **Best-fit** — take the *smallest* chunk that is big enough, so the leftover after splitting is as
  small as possible. Less wasted space per allocation, but you must scan to find the smallest.
- **Worst-fit** — take the largest (keeps leftovers big enough to be useful); rarely used in practice.

Lea's allocator is best-fit, organised so the scan is cheap:

> "Available chunks are maintained in bins, grouped by size." `[LEA]`
> "Searches for available chunks are processed in smallest-first, best-fit order." `[LEA]`

The exercise uses **first-fit** over a single list, because it is the clearest correct starting point
and makes the split/coalesce logic the star. A drill asks you to switch it to best-fit and measure
the fragmentation difference, which is the real lesson: *policy is a tuning knob layered on top of the
same mechanism.*

## 3. Splitting an oversized chunk

If the chosen free chunk is bigger than the request, handing the whole thing over wastes the
remainder. Instead `malloc` **splits** it: carve the front into an in-use chunk of the requested size,
and turn the tail into a *new, smaller free chunk* left on the list. Lea: chunks "are created (by
splitting larger chunks) only when explicitly requested" `[LEA]`.

The one rule that keeps splitting safe: **only split if the remainder is itself a legal chunk** — at
least the minimum chunk size (§7.2 §3: header + two-pointer payload, rounded to 16). If the leftover
would be smaller than that, you can't form a valid free chunk from it, so you leave it attached to the
allocation (the allocation gets a few bytes of slack — more internal fragmentation, §7.4). The
exercise's `split` encodes exactly this `remainder >= MIN_BLOCK` test; getting the boundary condition
right is most of the difficulty, and the tests hammer it.

```
before:  ┌───────────────── free, size 96 ─────────────────┐
request 32 →
after:   ┌─ in-use, size 48 ─┐┌──── free, size 48 ─────────┐   (48 = 32 payload + overhead, 16-rounded)
         returned to caller    stays on the free list
```

## 4. How glibc shards the one list into bins

glibc does not keep a single list — one list would get long and slow, and would force coalescing work
on every free. It keeps several kinds of bin, each a specialised free list `[GLIBC-malloc.c, LEA]`.
You don't implement these, but you must be able to name them when you read a heap in a debugger:

- **fastbins** — singly-linked lists of *small* chunks (up to `M_MXFAST`; "the upper limit for memory
  allocation requests that are satisfied using fastbins" `[MAN-mallopt]`). For speed, chunks in
  fastbins are **not coalesced** immediately and keep their `PREV_INUSE` neighbours thinking they're
  in use — a deliberate deferral that trades some fragmentation for raw allocate/free throughput.
  (This deferral is what the "fastbin dup" exploit class abuses, §7.5.)
- **unsorted bin** — a single scratch list where just-freed chunks land first; the next `malloc`
  sifts them into small/large bins as it searches. A one-list staging area that amortises sorting.
- **small bins** — many lists, each holding chunks of *one* exact size (e.g. 32, 48, 64, …). Exact-fit
  in O(1): pick the bin for the size, take the head.
- **large bins** — lists holding a *range* of sizes, kept sorted (that's what `fd_nextsize`/
  `bk_nextsize` in the §7.2 struct are for), searched best-fit within the bin.
- **the top chunk ("wilderness")** — not really a bin: the single chunk bordering the unallocated
  slab, used to carve new memory when no bin can serve you.

  > "The 'wilderness' (so named by Kiem-Phong Vo) chunk represents the space bordering the topmost
  > address allocated from the system." `[LEA]` … "the wilderness chunk always being used only if no
  > other chunk exists." `[LEA]`

  If even the top chunk is too small, *that* is when `malloc` finally goes back to the kernel (§7.1):
  `sbrk` to grow the break, or `mmap` for a big request.

(There is a newer per-thread cache, **tcache**, in front of all this since glibc 2.26 — a small
array of singly-linked bins owned by each thread, so most allocations never touch the shared arena or
take a lock. It behaves like fastbins for our purposes and matters in §7.5 for safe-linking.)

## 5. The order of operations, assembled
`malloc(n)` in one breath, now that the pieces have names:
1. round `n` up to a valid chunk size (§7.2 §3);
2. check the matching fast/tcache bin → exact small bin → scan unsorted/large bins best-fit;
3. if a chunk is found and it's oversized, **split** (§3);
4. if nothing fits, carve from the **top** chunk;
5. if top is too small, **`sbrk`/`mmap`** for more and carve from that (§7.1);
6. mark in use (clear the next chunk's... no — *set* the next chunk's `PREV_INUSE`), return payload.

`free(p)` in one breath (detailed in §7.4):
1. find the chunk from `p` (§7.2 §4);
2. (small enough? → drop in fastbin/tcache, done — deferred coalescing);
3. else **coalesce** with free neighbours (§7.4), put the merged chunk on the unsorted bin;
4. if the chunk now at top is huge, maybe **trim** back to the OS (§7.4).

The exercise implements the honest core of this — one list, round-up, first-fit, split, and real
coalescing on every free — which is enough to allocate and free arbitrary workloads correctly.

## Drills
1. On paper, run first-fit vs best-fit on free list `[16, 64, 32]` (sizes) for the request sequence
   `malloc(30), malloc(12), malloc(50)`. Which policy fails an allocation, and why? This is the
   fragmentation trade-off in miniature.
2. Why are fastbins singly-linked while small/large bins are doubly-linked? (Hint: fastbins never
   coalesce, so they never need to remove a chunk from the *middle* — §7.4 needs `bk` for that.)
3. The exercise's `split` refuses to split when the remainder `< MIN_BLOCK`. Construct a request size
   where splitting a 64-byte free chunk is refused, and say where the extra bytes go.
4. After reading §7.4, come back and explain why putting a just-freed chunk in a fastbin (no
   coalescing) can leave two adjacent free chunks un-merged — and when they finally do merge.

## My summary
