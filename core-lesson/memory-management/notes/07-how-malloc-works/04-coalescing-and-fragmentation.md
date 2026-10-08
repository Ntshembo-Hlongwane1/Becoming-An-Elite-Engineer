# 7.4 — Coalescing and fragmentation

Splitting (§7.3) makes chunks *smaller* over time: a big free chunk gets chipped into small
allocations. If nothing reversed that, the heap would grind into dust — thousands of tiny free chunks,
none big enough for the next real request, even though their total is huge. **Coalescing** is the
reverse operation that keeps the heap healthy.

## 1. Coalescing = merging adjacent free chunks

When you `free` a chunk, `malloc` checks its two physical neighbours (found in O(1) by the
boundary-tag arithmetic of §7.2 §5). Any neighbour that is *also* free gets merged with it into one
larger chunk. Lea states both the goal and the aggressiveness:

> "Two bordering unused chunks can be coalesced into one larger chunk." `[LEA]`
> "each freed chunk is immediately coalesced with neighbors to form the largest possible unused
> chunk." `[LEA]`

The merge is pure bookkeeping: add the neighbours' sizes, keep the lowest address as the merged
chunk's header, write the new size into the header (and the footer/trailer), and fix the free-list
links. No bytes move. There are four cases for a freed chunk `C` with previous `P` and next `N`:

| `P` free? | `N` free? | result |
|---|---|---|
| no | no | `C` alone goes on the free list |
| no | yes | merge `C`+`N` → one chunk at `C` |
| yes | no | merge `P`+`C` → one chunk at `P` |
| yes | yes | merge `P`+`C`+`N` → one chunk at `P` |

The exercise's `coalesce(C)` implements exactly these four cases; it's the hardest function in the
exercise and the one the "free everything ⇒ one big chunk again" test checks directly.

## 2. How a chunk knows if its neighbour is free: `PREV_INUSE`

Finding the *next* chunk is easy — it starts at `C + size(C)` — and you read its header to see if it's
free. Finding the *previous* one is the trick. glibc doesn't scan; it uses the `PREV_INUSE` bit
(§7.2 §2). The rule `[GLIBC-malloc.c]`:

> "If that bit is clear, then the word before the current chunk size contains the previous chunk
> size, and can be used to find the front of the previous chunk."

So: read `C`'s header; if `PREV_INUSE` is **clear**, the previous chunk is free, and the word just
below `C` (its `prev_size`/footer) holds the previous chunk's size → the previous header is at
`C - prev_size`. If `PREV_INUSE` is **set**, the previous chunk is in use; leave it alone. One bit,
read in O(1), answers "can I merge leftward?" — no extra table, no scan. That is the whole payoff of
the boundary-tag design.

The exercise uses the simpler, equivalent scheme most teaching allocators use: a **footer** at the end
of every chunk mirroring the header's size and free flag, so `prev = (read footer just below C) →
header at C - that_size`, and you read that header's free flag directly. Same O(1) result, one bit
easier to reason about; a drill relates it back to glibc's single-bit version.

## 3. Internal vs external fragmentation

Fragmentation is wasted capacity, and there are two distinct kinds — name them correctly, because the
fixes differ:

- **Internal fragmentation** — waste *inside* a chunk: you asked for 100 bytes, the chunk gives 104
  usable + overhead (§7.2 (measured)), so 4+ bytes are yours-but-unused, plus the header. Caused by
  rounding to alignment and the minimum chunk size. It's bounded and predictable. The fix is smaller
  size classes / tighter rounding — a *policy* choice.
- **External fragmentation** — waste *between* chunks: enough total free memory exists, but it's split
  into pieces none individually large enough for the next request. Your book names it:

  > "Fragmentation: … This occurs when small, unused chunks of [memory accumulate]." `[MEM §1.1]`
  > "large dynamic arrays may lead to memory fragmentation, especially when allocating and
  > deallocating many small blocks of memory." `[MEM §10.x]`

  Coalescing (§1) is the primary defence against external fragmentation; placement policy (§7.3 §2)
  is the secondary one. Pools/arenas (Lesson 10) sidestep it entirely for fixed-size objects.

A pathological external-fragmentation sequence to internalise: allocate `A,B,C,D` adjacent, free
`A` and `C`. You now have two 1-chunk holes with `B` wedged between them — a request bigger than one
hole fails though the free total is two holes. Nothing is wrong with the allocator; the *lifetime
pattern* fragmented it. This is why long-running servers restart, and why custom allocators (the rest
of Phase B) exist.

## 4. Why `free` keeps the memory instead of returning it

A surprise for beginners: after `free`, your process's memory footprint usually does **not** shrink.
`malloc` holds onto freed chunks on its lists to satisfy the next request without a syscall. It
returns memory to the OS only in two specific situations:

- **A big (`mmap`ed) chunk** (§7.1) is `munmap`ed on `free` — that genuinely returns.
- **The top chunk grows large enough**: when contiguous free memory at the top of the heap exceeds
  `M_TRIM_THRESHOLD` (128 kB default), `free` trims it back with `sbrk`. `mallopt(3)`:

  > "When the amount of contiguous free memory at the top of the heap grows sufficiently large,
  > `free(3)` employs `sbrk(2)` to release this memory back to the system." `[MAN-mallopt]`
  > "The default value for this parameter is 128*1024." `[MAN-mallopt]`

The break only moves at the **top** (§7.1 §3), so a freed chunk trapped below in-use chunks can never
be trimmed — it can only be reused via the free list. This is the mechanical reason the ((measured))
4 MB of requests grew the break 5.27 MB and *stayed* there after the pattern below it was freed. For
your capstone and red-team write-ups this matters: RSS (Lesson 4.2) staying flat after a leak is
fixed does **not** mean the fix failed — the allocator is just holding the slab.

## 5. The wilderness, one more time
The top chunk (§7.3 §4) is where growth and trimming happen — it borders the raw slab, so it's the
only chunk that can cheaply get bigger (grow the break) or smaller (trim). Everything else is a fixed
tiling of chunks that split and coalesce among themselves. That asymmetry — a mutable frontier plus a
coalescing interior — is the whole shape of a heap, and it's what your exercise reproduces in
miniature: one arena, a top-ish frontier, and coalescing everywhere below it.

## Drills
1. Implement the §3 pathological pattern against the real `malloc`: alloc four 48-byte blocks, free
   the 1st and 3rd, then try to `malloc(80)`. Does it reuse a hole or grow the heap? Print addresses.
2. In the four-case table (§1), why is the "P free, N free" merge done as one operation rather than
   "merge with P, then merge with N" separately? (Think about the free-list links and what each merge
   has to unlink — the exercise's `coalesce` must not leave a dangling link.)
3. Relate the exercise's footer-mirror scheme (§2) to glibc's single `PREV_INUSE` bit: what does the
   footer store that the bit scheme instead *infers*, and what does glibc save by not storing it on
   in-use chunks? (§7.2 §2, the overlap.)
4. You fix a leak; RSS doesn't drop. Give the two-sentence correct explanation to a teammate using
   §4, and name the one `mallopt` knob that *would* make it drop.

## My summary
