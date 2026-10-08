# 2.6 — The Security Researcher's View: Lifetime & Type as Exploit Primitives

Lesson 1 made *integers* the root cause. This lesson's bugs — **use-after-free**, **out-of-bounds**,
**type confusion** — are the *primitives* an attacker builds on top of that root. Your capstone is a
tool that detects exactly these, so understand them from both sides. Three hats.

## Primitive A: Use-after-free (UAF)

The bug (§2.5): an object is freed, a pointer to it survives, and it's used again. Why it's a
*primitive*, not just a crash:

- **Offense — discovery.** Source review: a raw pointer or reference that outlives the `delete`/
  scope/`reset()` that frees its target; a cached pointer into a container that later reallocates
  (§2.5 §5). Black-box/dynamic: run under ASan and fuzz — ASan's quarantine (which your capstone
  reimplements) makes the freed block stay poisoned so the use is caught at the moment it happens.
- **Offense — value (ceiling).** The exploitation recipe: free the object, then get the allocator to
  hand that same block back for a *different* object you control the contents of (heap grooming), so
  the dangling pointer of the old type now points at attacker-chosen bytes. If the old type had a
  function pointer / vtable, you may redirect control flow. **Ceiling: often code execution.** Floor:
  a crash (DoS) or an info leak if you only get to read. Saying which, for a given UAF, is the real
  analysis.
- **Defense — impossible by construction.** Ownership (Lessons 11–12): a `unique_ptr` has no valid
  pointer after it's moved-from or reset; `shared_ptr`/`weak_ptr` express "may have gone". Don't cache
  raw pointers across operations that can free. In the capstone, the **quarantine + shadow poison**
  is the detection; in production, ownership is the prevention.

## Primitive B: Out-of-bounds (OOB) read/write

The bug (§2.3): a pointer/index walks past `[0, n)`. `[MEM §5.1]`'s stack and heap overflow examples.
- **Offense — discovery.** Any length/index not bounds-checked against the real size; the Lesson-1
  integer bug that produces a bad length feeds straight into here. Fuzz + ASan red zones (your
  capstone's red zones) catch the first byte past the end.
- **Offense — value.** An OOB **write** past a heap block corrupts the *next* block's allocator
  metadata or object — the classic heap-overflow path to control of a later `malloc`/`free`
  (`[MEM §5.1]`: "corrupt memory management structures"). An OOB **read** leaks adjacent memory
  (Heartbleed shape). Ceiling: write → code execution; read → info disclosure.
- **Defense.** Carry the length with the pointer (`std::span`, §2.2 §5), check with overflow-safe
  arithmetic (Lesson 1), and let the container own bounds (Lesson 13). Your `ByteCursor` exercise is
  this defence in miniature.

## Primitive C: Type confusion (the aliasing bug weaponised)

§2.4's strict-aliasing violation, taken to its conclusion: the program treats one type's bytes as
another type's object. If the "object" contains a pointer, length, or vtable, and the attacker
controls the underlying bytes, they control that field.
- **Offense — discovery.** `reinterpret_cast` of untrusted bytes into a struct with pointers; unions
  read through the wrong member; downcasts without checking the real type.
- **Offense — value.** Fabricate a pointer or size the program then trusts. Ceiling: code execution /
  arbitrary read-write.
- **Defense.** Never reinterpret untrusted bytes into a live object; `memcpy`/`bit_cast` into a
  trivially-copyable type you then **validate** (§2.4 §3), and decode fields with the Lesson-1 codec,
  bounds-checked.

## Through-line to your capstone and project
- Your **capstone detector** exists to catch Primitives A and B at runtime (shadow memory + red
  zones + quarantine). Building it means internalising exactly how these primitives manifest in
  memory.
- When you **red-team your object store**, these are the first three things to hunt: a cached pointer
  that outlives a free, a length that isn't bounds-checked, and any `reinterpret_cast` of file/network
  bytes.

## My summary
