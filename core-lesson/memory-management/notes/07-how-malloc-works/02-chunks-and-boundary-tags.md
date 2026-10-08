# 7.2 — Chunks: the header, the flags, and boundary tags

Now `malloc` has a big slab of raw bytes. It has to cut it into pieces and remember, for each piece,
how big it is and whether it's in use — otherwise `free(ptr)` could not know how many bytes `ptr`
owns, and the allocator could never reuse anything. The unit it cuts into is the **chunk**.

## 1. A chunk = header + payload

Every allocation is a **chunk**: a small **header** that `malloc` reads and writes, followed by the
**payload** — the bytes it hands to you. `malloc` returns a pointer to the payload; the header lives
just *before* that pointer, invisible to you. Doug Lea's allocator (the design glibc descends from)
states the core idea:

> "Chunks of memory carry around with them size information fields both before and after the chunk."
> `[LEA]`

"Before and after" is the **boundary-tag** method (§5). The header is the primary size field; the
copy *after* is the trailer used for coalescing. First, the header.

## 2. The header carries the size — and three flag bits

glibc's chunk header is this struct `[GLIBC-malloc.c]`:

```c
struct malloc_chunk {
  INTERNAL_SIZE_T  mchunk_prev_size;  /* Size of previous chunk (if free).  */
  INTERNAL_SIZE_T  mchunk_size;       /* Size in bytes, including overhead.  */
  struct malloc_chunk* fd;            /* double links -- used only if free.  */
  struct malloc_chunk* bk;
  struct malloc_chunk* fd_nextsize;   /* double links -- used only if free.  */
  struct malloc_chunk* bk_nextsize;
};
```

Read it carefully, because it is denser than it looks:
- `mchunk_size` — **the chunk's total size in bytes, including the overhead** `[GLIBC-malloc.c]`. This
  is *the* field `free` needs. Because `malloc` keeps all chunk sizes aligned to a multiple of 16
  (§3), the low 4 bits of `mchunk_size` are *always zero* for the size — so `malloc` **steals those
  low bits to store status flags** `[LEA, GLIBC-malloc.c]`. Three are defined:

  | Bit | Name | Meaning `[GLIBC-malloc.c]` |
  |---|---|---|
  | `0x1` | `PREV_INUSE` (P) | the *previous* (lower-address) chunk is in use |
  | `0x2` | `IS_MMAPPED` (M) | this chunk came from its own `mmap` (§7.1) |
  | `0x4` | `NON_MAIN_ARENA` (A) | this chunk belongs to a secondary arena (threads) |

  To get the real size you mask them off: `size = mchunk_size & ~0x7`. The `PREV_INUSE` bit is the
  clever one — it lets a chunk answer "is my lower neighbour free?" without storing a separate
  boolean, which §5 and §7.4 depend on.
- `mchunk_prev_size` — "Size of previous chunk (if free)." `[GLIBC-malloc.c]` This is the *trailer*
  from §1, but stored at the *front* of the next chunk. Here is the trick that saves 8 bytes per live
  chunk: **when a chunk is in use, its neighbour doesn't need this field, so the neighbour lets the
  in-use chunk's payload overlap it.** The comment block says the `prev_size` word is only meaningful
  when the previous chunk is free; otherwise it's part of the previous chunk's user data
  `[GLIBC-malloc.c]`. You don't need to reproduce that micro-optimisation (the exercise doesn't), but
  you must recognise it: it's why a chunk header looks like "only 8–16 bytes" even though it logically
  has both a leading and trailing size.
- `fd`, `bk`, `fd_nextsize`, `bk_nextsize` — "**used only if free**" `[GLIBC-malloc.c]`. These are the
  free-list links (§7.3). The insight that makes allocators cheap on memory: **a free chunk's payload
  is garbage to you, so `malloc` reuses that same payload space to store its linked-list pointers.**
  The links cost nothing extra — they live inside space that is, by definition, not in use. This is
  why the *minimum* chunk must be big enough to hold two pointers (§3).

So one piece of memory wears two hats over its life: while allocated, the bytes after the header are
*your* payload; the instant you `free` it, `malloc` writes `fd`/`bk` into those very bytes. (Hold on
to that — it is the mechanism behind use-after-free and the fastbin/tcache attacks in §7.5.)

## 3. Alignment and the minimum chunk size

`malloc` must return a pointer "suitably aligned for any type that fits into the requested size or
less" `[MAN-malloc]` — i.e. aligned to `alignof(max_align_t)`. On your VM that is **16** ((measured),
Lesson-6 era `m.cpp` and §4 below). glibc's chunk alignment is `2 * sizeof(size_t)` `[GLIBC-malloc.c]`,
which is 16 on a 64-bit machine. Two consequences:

- **All chunk sizes are rounded up to a multiple of 16**, which is what frees the low bits for flags
  (§2) and guarantees every payload pointer is 16-aligned.
- **There is a minimum chunk size.** Lea: "the smallest allocatable chunk is 16 bytes in systems with
  32-bit pointers and 24 bytes in systems with 64-bit pointers," and "the 16 bytes minimum at least is
  characteristic of any system requiring 8-byte alignment in which there is any malloc bookkeeping
  overhead." `[LEA]` The reason is §2's free-list links: even a `malloc(1)` chunk must, once freed,
  hold an `fd` and a `bk` pointer, so it can't be smaller than two pointers plus overhead.

This is the origin of **internal fragmentation** (§7.4): ask for 1 byte, get a whole minimum chunk.

### (measured) the overhead and the rounding, on your VM
```
malloc_usable_size(100-byte req) = 104       # you asked 100, the chunk's payload is 104
returned ptr % 16 = 0   % 8 = 0              # 16-byte aligned, as promised
malloc(24) x=0x...c090 y=0x...c0b0  |y-x|=32  usable(x)=24
```
- `malloc(100)` gives a payload of **104** usable bytes — rounded up so the *whole* chunk
  (payload + overhead) lands on a 16-byte boundary **(derived)**. `malloc_usable_size` reports the
  payload, not the overhead.
- Two consecutive `malloc(24)`s sit **32 bytes apart**. You asked for 24; the chunk is 32 (`24` payload
  usable, confirmed by `malloc_usable_size`, + 8 bytes of header that overlaps as described in §2),
  and 32 is the next 16-multiple ≥ what's needed **(derived)**. The 32-byte stride *is* the chunk
  size. Source: `m1.cpp`.

## 4. The two pointers: chunk pointer vs user pointer

Keep these distinct — half of all heap bugs are confusing them:
- **user pointer** `p` — what `malloc` returns and you use. 16-aligned.
- **chunk pointer** — `p` minus the header size — where `malloc`'s bookkeeping starts.

`free(p)` first does `chunk = p - header` to find the header, reads `chunk->size` (masking the flag
bits), and from that knows the chunk's extent. **This is why you must pass `free` exactly the pointer
`malloc` returned** — pass `p+8` and `free` reads garbage where the size should be, and corrupts the
heap. The exercise enforces this with an assertion, which is also what real hardened allocators do.

```
   chunk ptr                      user ptr (returned to you, 16-aligned)
      │                              │
      ▼                              ▼
      ┌───────────┬──────────────────────────────────────┬──────────┐
      │  header   │  payload (your bytes; usable_size)    │ (trailer)│
      │ size|flags│  ... reused as fd/bk when freed ...    │ next's   │
      └───────────┴──────────────────────────────────────┴ prev_size┘
      └────────────────────── chunk size (multiple of 16) ───────────┘
```

## 5. Boundary tags: why this layout makes `free` O(1)

Return to Lea's "size information fields both before and after the chunk" `[LEA]`. Because each chunk
knows its own size (header) and the chunk physically after it begins at `chunk + size`, `malloc` can
walk *forward* from any chunk to its right neighbour by pure address arithmetic. And because a free
chunk's size is also deposited at its tail (the next chunk's `prev_size`), `malloc` can find the
chunk physically *before* a given one by reading the word just below it. So from one chunk it can
reach **both** neighbours in constant time — no scanning, no central table keyed by address. That is
the boundary-tag method, and it is precisely what makes coalescing (§7.4) cheap enough to do on every
`free`:

> "Two bordering unused chunks can be coalesced into one larger chunk." `[LEA]`

The exercise implements exactly this: a header carrying size+flag, a footer carrying the size, and
`next`/`prev` navigation by arithmetic. Get the layout right and split/coalesce become short.

## Drills
1. Why can't the chunk size field's low 3 bits ever collide with a real size value? State the
   invariant (§3) that guarantees it, and what would break if a single allocation of 17 bytes were
   *not* rounded up.
2. `malloc_usable_size(malloc(1))` on your VM — predict, then measure. How many bytes did a 1-byte
   request really cost including overhead? Relate to the minimum chunk size (§3).
3. Explain, using §2, why reading one byte *before* the pointer `malloc` gave you is reading
   `malloc`'s own metadata — and why writing there is a classic heap corruption even though you never
   touched "someone else's" allocation.
4. In the diagram (§4), the trailer says "next's prev_size". Under what condition is that word
   *not* used as a size, but as this chunk's payload instead? (§2, the overlap trick.)

## My summary
