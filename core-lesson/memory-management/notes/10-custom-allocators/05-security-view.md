# 10.5 — The Security Researcher's View: custom allocators cut both ways

A custom allocator changes the heap's security properties — sometimes for the worse, sometimes
decisively for the better. Unlike Lessons 7–8, there is no single "the bug"; the theme is that
**by replacing the allocator you move where the bodies are buried**, and a researcher must know which
way a given design moved them. Three hats.

## The double edge (what changes when you swap `malloc` for an arena/pool)
- **Your sanitizer goes partly blind.** AddressSanitizer works by instrumenting `malloc`/`free`
  (Lesson 8.4 §4): it red-zones each allocation and poisons freed memory. But an arena or pool carves
  objects out of **one big buffer that `malloc` handed out once**. To ASan that whole buffer is a
  single live allocation — so an overflow from one arena object into the next, or a use-after-"free"
  of a pool block, stays *inside* the known buffer and ASan says nothing. You traded `malloc`'s bug
  detection for speed without realising it. (This is why allocator authors add their own red-zones;
  §defense.)
- **"Free" may not mean free.** An arena's `deallocate` is a no-op and `reset()` runs **no destructors**
  (§10.2 §3). So "I freed it" can mean the bytes are still there, unchanged, holding stale secrets, or
  that an object's owned resources leaked. A pool's `deallocate` recycles the exact block (§10.3 §4),
  so a **use-after-free reads/writes a now-live different object** — the Lesson-7.5 UAF, but with no
  allocator metadata in between to trip a check.
- **No hardening by default.** glibc spent years adding safe-linking, tcache keys, and size checks
  (Lesson 7.5). Your hand-rolled pool has an intrusive `next` pointer in free blocks (§10.3 §1) and
  *none* of that — it is glibc's fastbins circa 2004, i.e. a clean fastbin-dup / pointer-overwrite
  target.

## Offense — discovery
- **Source/RE:** find the custom allocator (a class with `allocate`/`deallocate`, a `memory_resource`,
  a bump pointer or an intrusive free list) and read *its* rules, not libc's. Look for: `reset()` with
  live non-trivial objects (resource leak / stale data); `deallocate` that recycles (UAF aliasing);
  a free-block `next` pointer you can reach via an overflow of an adjacent *live* block (pool poison);
  no red-zones between blocks (silent overflow).
- **Dynamic:** ASan under-reports here (above), so corruption often shows only as a logic bug —
  object A's field changing when you touch object B (overflow within the arena), or two "distinct"
  objects sharing storage (pool UAF). Recognising that the allocator is the reason is the skill.

## Offense — value, honest ceiling
- **Ceiling:** a pool's intrusive free list gives the same primitive as glibc fastbins pre-hardening —
  overwrite a free block's `next` (via UAF-write or overflow of a neighbour) and the next `allocate`
  returns an **attacker-chosen address**, i.e. arbitrary placement → arbitrary write (Lesson 7.5). An
  arena overflow is a *linear* overwrite into whatever object was bumped next — powerful if that's a
  function pointer or a length, contained if it's inert data.
- **Floor / obstacles:** it depends entirely on the design. A pool of **inert, fixed-size** records
  with no pointers, overflow caught by a red-zone, is nearly inert. The honest statement names the
  specific design feature that gates escalation ("the free list is intrusive and unmasked, so a single
  UAF-write is a write-what-where"), exactly as §7.5 did for glibc.

## Defense — and the containment the arena buys you
The same properties that blind ASan can be turned into strong, *by-construction* defences — this is
where custom allocators shine for a security engineer:
- **Isolation / partitioning.** Put attacker-influenced objects in their **own arena/pool**, separate
  from pointers and control data. An overflow then corrupts only same-class objects, never a return
  address or vtable. This is the core idea behind partitioned/type-isolated heaps (PartitionAlloc in
  Chromium, `kalloc_type` in XNU): the allocator *is* the mitigation.
- **Wipe on free/reset.** An arena `reset()` or pool `deallocate` can `memset(0)` the reclaimed bytes,
  so freed secrets don't linger and stale pointers become null — cheap because you control the path.
- **Add your own red-zones + guard pages.** Put a poisoned gap between blocks and a `PROT_NONE` guard
  page (Lesson 4.4 `mprotect`) at the arena's end; now a linear overflow faults instead of silently
  corrupting — you've rebuilt ASan's protection for your buffer.
- **Bounds by construction.** `monotonic_buffer_resource{buf, size, null_memory_resource()}` (§10.4 §3)
  cannot exceed `buf` — it throws instead of growing, a hard cap useful in embedded/sandboxed code.
- **Still use RAII** (Lesson 11): the object lifetime discipline doesn't change just because the bytes
  come from an arena — a `unique_ptr` with a custom deleter that calls the pool's `deallocate` gives
  you the pattern safely.

The meta-lesson for your track: **the allocator is a security-policy surface.** An attacker who
replaces libc's hardening with a naive pool hands you bugs; a defender who replaces libc's one-size
heap with partitioned arenas + guard pages takes whole bug classes off the table. You will build both
sides — the naive pool here, the hardened allocator in Lesson 17.

## Through-line to the capstone
Your capstone detector red-zones and guard-pages allocations — which is precisely the "add your own
red-zones" defence above, applied as a tool. And when you analyse a target that ships a custom
allocator (game engines, browsers, kernels all do), step one is reconstructing *its* rules, because
ASan and your libc instincts won't apply. This lesson is where that reflex starts.

## My summary
