# 7.5 — The Security Researcher's View: the Heap Is Metadata

The stack smash (§5.5) hijacks a *return address* that the hardware already treats as code. The heap
has no return addresses — so why is it the richest exploitation surface of the last fifteen years?
Because of the one fact this whole lesson built toward: **`malloc`'s bookkeeping lives inline, right
next to your data, in the same writable slab.** Chunk headers, free-list `fd`/`bk` pointers, sizes and
flags — all of it sits bytes away from buffers you fill with input. Corrupt the *data* and you corrupt
the *allocator's control structures*, and from there you make `malloc` itself hand out a pointer of
your choosing. Your book sets the scene:

> "A heap overflow occurs when data is written beyond the bounds of a block of memory allocated on the
> heap. … Heap overflows are particularly dangerous [because they can corrupt] the metadata the
> system uses to track heap memory." `[MEM §1.x, Heap Overflows]`

Three hats, on three bugs that are all really *one* bug — "the allocator trusted memory an attacker
could write."

## The three primitives (what goes wrong)
- **Heap buffer overflow** — write past a chunk's payload into the *next chunk's header* (§7.2 §4:
  the header sits just past your payload). You overwrite a size or the `PREV_INUSE`/flag bits, so the
  allocator's picture of chunk boundaries is now a lie you wrote.
- **Use-after-free (UAF)** — keep using a pointer after `free`. Recall §7.2 §2: the instant you free a
  chunk, `malloc` writes `fd`/`bk` *into the payload you still hold a pointer to*. So a UAF read leaks
  allocator pointers (defeating ASLR, Lesson 3.4), and a UAF write edits free-list links.
- **Double-free** — `free` the same chunk twice. The chunk ends up on a free list twice, so a later
  `malloc` hands the *same* address out for two different live objects — an attacker-controlled alias.

## Offense — discovery
- **Source/RE:** an allocation sized from input (`malloc(n*sz)` — the Lesson-1 integer overflow feeds
  this), a copy into a heap buffer without a length bound (§5.5's `strcpy` family, now on the heap), a
  freed pointer that isn't nulled (`free(p)` without `p=nullptr`), or two owners of one pointer (the
  rule-of-three/five gap — Lesson 11). Grep targets: `free(` followed by later use; `malloc(` with an
  arithmetic size.
- **Dynamic:** AddressSanitizer is the researcher's first tool here. It reports `heap-buffer-overflow`,
  `heap-use-after-free`, and `double-free` with both the faulting access *and* the allocation/free
  stacks (§7.6 is built on this). Fuzzing under ASan turns "maybe a bug" into a precise report.
- **Heap grooming / "feng shui":** the real skill is *arranging* the heap — allocating and freeing in
  a pattern (using §7.3's bin rules) so the chunk you can overflow lands physically before the chunk
  whose header you want to corrupt. That's applied §7.2–§7.4, not magic.

## Offense — value, honest ceiling
- **Ceiling:** the strong heap primitives give **arbitrary write** ("write what/where") and often
  **code execution**. The canonical fastbin/tcache attack: free a chunk so it enters a singly-linked
  bin (§7.3 §4), corrupt its forward pointer (via UAF or overflow) to point at a target address, then
  `malloc` twice — the second `malloc` returns *your* target address as a normal heap pointer, and now
  writing to that "allocation" writes wherever you aimed. Named techniques (studied here as mechanism,
  **not** reproduced as weaponized chains): *fastbin dup*, *tcache poisoning*, *House of Spirit/Force/
  Einherjar*, and the historical *unlink* attack that abused the `fd`/`bk` doubly-linked unlink in
  `coalesce` (§7.4) itself. The classic references are Phrack's *"Vudo malloc tricks"* and *"Once upon
  a free()"*, and Blackeng's *"Malloc Maleficarum"* — named for study, mechanism derived in notes.
- **Floor / obstacles:** modern glibc fights back (below), so a single heap bug today usually needs a
  companion **info leak** to beat ASLR and safe-linking. As in §5.5, the researcher's honesty is
  stating exactly what's still required to escalate — "UAF read for a libc leak, then tcache poison
  for the write" — rather than claiming instant RCE.

## Defense — and what makes it impossible by construction
Allocator hardening has turned most of the old one-shot techniques into "needs a second bug":
- **Safe-Linking (glibc ≥ 2.32):** single-linked `fd` pointers in fastbins/tcache are stored XORed
  with a secret derived from their own address (`ptr ^ (addr >> 12)`), so an attacker who overwrites
  one must *know* that address first — a naive pointer overwrite now produces a garbage,
  alignment-checked pointer and aborts. `[SAFELINK]`
- **tcache key / double-free detection:** freed tcache chunks carry a per-thread "key"; freeing one
  that already bears the key is caught as a double-free. fastbins likewise check "double free or
  corruption (fasttop)".
- **Size/alignment sanity checks** throughout `malloc`/`free` ("corrupted size vs. prev_size",
  "invalid next size") reject many forged headers (§7.2's boundary tags are also a *check* surface).
- **AddressSanitizer** (dev/CI) and **guard pages** (Lesson 4.4 `mprotect`) catch the overflow/UAF at
  the moment it happens — this is literally what your capstone detector does.
- **Hardened allocators** that move metadata *out of line* so a payload overflow can't reach a header
  at all — OpenBSD's `malloc`, and Daniel Micay's **hardened_malloc** (used by GrapheneOS). This is
  the "by construction" fix: if the bookkeeping isn't adjacent to the data, overflowing the data can't
  corrupt the bookkeeping. Your Lesson-7 exercise keeps metadata inline (to *teach* the classic
  layout); Lesson 17 has you harden it — canaries between chunks, out-of-line headers, free-list
  pointer checks — turning this section's attacks into aborts.
- **Language-level:** smart pointers + RAII (Lessons 11–12) make UAF/double-free *unrepresentable* for
  owned memory (one owner, freed exactly once, pointer dies with the object). `[MEM §1.3.1]` prescribes
  exactly this as the fix for the leak/UAF family.

## Through-line to the capstone
This is the center of your security track. The capstone heap detector catches the §"primitives" above
with shadow memory + guard pages, and reports them by unwinding the stack (§5.3). Lesson 17 then has
you *harden the very allocator you build next* against the fastbin/unlink attacks named here — so you
will have written the vulnerable allocator, the detector that catches abuse of it, and the hardening
that closes it. That full loop — build it, break it, defend it — is exactly the researcher capability
you're training for, grounded in code you wrote rather than an abstract CTF.

## My summary
