# 17.1 — The attacker's model of the heap

To harden an allocator you must first see it the way an attacker does. This file assembles the heap
primitives you met across the course into one picture: the pipeline that turns a single memory bug
into control of the program.

## 1. The surface: your data sits next to the allocator's metadata

Lesson 7.2 established the fact everything here rests on: the allocator's bookkeeping — chunk sizes,
flags, free-list pointers — lives **inline, next to your data**, in the same writable region. So a bug
in *your* data reaches *its* metadata. Your book states the consequence `[MEM §1.x]`: "Heap overflows
are particularly dangerous [because they can corrupt] the metadata the system uses to track heap
memory."

## 2. The four primitives (recap of Lessons 7.5 / 8.5 / 13.5 / 15.5)

- **Heap overflow → metadata corruption.** Write past a chunk's payload into the next chunk's header
  (size, flags) or a free chunk's `fd`/`bk` pointer. The source of the bad length is often the Lesson-1
  integer bug or the Lesson-13 held-across-mutation pattern.
- **Use-after-free (UAF).** Keep using a pointer after `free`; the allocator may have written free-list
  links into those bytes (Lesson 7.2 §2) or handed the chunk to another object → read leaks pointers,
  write edits the allocator or the new object.
- **Double-free.** Free the same chunk twice → it's on a free list twice → `malloc` hands the same
  address to two owners (an attacker-controlled alias).
- **Free-list pointer injection.** Via UAF-write or overflow, overwrite a freed chunk's forward
  pointer; the next `malloc` returns the attacker's chosen address (**fastbin/tcache poisoning**; the
  classic **unlink** attack abuses the doubly-linked unlink in coalescing). **ABA** (Lesson 15.3) is
  the lock-free cousin.

## 3. The exploitation pipeline

A bug is not an exploit; the attacker walks a pipeline, and each stage is where a defence can break the
chain:
1. **Trigger** a memory bug (overflow / UAF / double-free) — often reachable through ordinary API use
   (Lesson 13.5's held-across-mutation, Lesson 11.5's broken rule-of-five).
2. **Groom the heap** ("feng shui"): allocate/free in a pattern (using the allocator's own bin/reuse
   rules, Lesson 7.3) so the chunk you can corrupt sits next to the chunk you want to control, or so a
   freed address comes back where you need it.
3. **Corrupt a target**: a size (to overlap chunks), a free-list pointer (to redirect the next
   allocation), a vtable/function pointer, or a length field.
4. **Escalate to a write primitive**: pointer injection → `malloc` returns a target address →
   writing to that "allocation" is a **write-what-where**.
5. **Hijack control**: overwrite a return address (Lesson 5.5), a vtable, a GOT/function pointer → code
   execution, typically via ROP (NX, Lesson 3.4, forbids injected shellcode).

## 4. Why custom allocators are especially juicy (Lesson 10.5)

A program that ships its own pool/arena is often *easier* to attack than one on hardened glibc:
- **The sanitizer is blind.** The arena is one big `malloc`'d region, so an overflow from one arena
  object into the next stays inside that region — **AddressSanitizer reports nothing** (Lesson 10.5).
  The bug ships because tests were green.
- **No hardening by default.** A hand-rolled pool has glibc's circa-2004 behaviour: inline, unmasked
  free-list pointers (Lesson 7.2) and no size checks — a clean pointer-injection target.
This is exactly why *your* allocator (this exercise) must harden itself: the stock tools won't.

## 5. The honest ceiling

A researcher states what a bug gives *and what it still needs* — the discipline this course's security
notes drilled. A lone heap bug today usually is **not** instant RCE: modern allocator hardening
(§§17.2–17.4), ASLR, and NX mean the attacker typically also needs an **information leak** (to defeat
ASLR and safe-linking), reliable grooming, and sometimes several bugs chained. The value of hardening
is precisely this: it moves the target from "one overflow → shell" to "needs a leak + grooming + a
second primitive," which stops most attackers and raises the cost for the rest. Hardening is **cost**,
not a proof of impossibility (§17.5) — except where a defence removes a primitive *by construction*
(isolation, guard pages for linear overflow).

## Drills
1. For each of the four primitives (§2), name the earlier lesson that first showed it and the one
   source-level pattern that most often introduces it (e.g. UAF ← escaped `.get()`/held-across-mutation).
2. Walk the pipeline (§3) for a fastbin/tcache poisoning on a hand-rolled pool: which stage does
   safe-linking (§17.3) break, and which does a guard page (§17.4) break?
3. Explain, using §4 and Lesson 10.5, why "our allocator passes ASan" is *not* evidence it's safe
   against intra-arena overflow. What must the allocator do that ASan can't?
4. Write the honest one-paragraph assessment of a UAF you found in a custom pool: the primitive, the
   grooming needed, and what still gates RCE (§5).

## My summary
