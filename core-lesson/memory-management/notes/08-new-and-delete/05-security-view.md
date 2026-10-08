# 8.5 — The Security Researcher's View: mismatched pairs, and `operator new` as a hook

Lesson 7 showed the heap's *metadata* as the attack surface. Lesson 8 adds two things to that picture:
a family of **bug classes born from mismatching `new`'s paired operations**, and the realisation that
the very replaceability that makes those bugs possible is also the **defender's best hook** — the one
leak detectors and sanitizers hang on. Three hats, as usual.

## The bug classes (all are "the two halves of `new`/`delete` got mismatched")
Recall §8.1: `new` = allocate+construct, `delete` = destruct+deallocate, and each *form* (`new` vs
`new[]`, which `operator`, which alignment) stores different bookkeeping. Mismatches:
- **`new` / `delete[]` (or `new[]` / `delete`)** — the array **cookie** (§8.1 §4) is misread: `delete[]`
  on a non-array pointer treats random bytes as a destructor count; `delete` on an array pointer frees
  8 bytes off the real allocation and destroys only one object. Your book flags both directions as UB
  `[MEM §2.2]`.
- **mismatched allocator family** — `free(new T)` / `delete (malloc(...))` (§8.1 §3): wrong
  deallocator, skipped destructor.
- **double-delete** — `delete p; delete p;` puts the chunk on a free list twice → the Lesson-7.5
  double-free primitive, now spelled in C++.
- **use-after-delete** — using `p` after `delete p`; identical to Lesson-7.5 UAF, because `delete`'s
  step 2 is just `operator delete` → `free`.
- **leak via missing manual destroy** (§8.3 §3) — placement-new without `~T()`: the *object's* owned
  resources leak even if the storage is reclaimed.

## Offense — discovery
- **Source/RE:** grep for `new[` whose pointer is later `delete`d without `[]` (and vice-versa); a
  class with `virtual` methods but a non-`virtual` destructor deleted through a base pointer (§8.1 §2);
  `free`/`delete` families crossed; a `delete p;` with `p` used or freed again on another path; raw
  `operator new`/placement-new in custom allocators (`[MEM]`'s pool/StorageSystem examples, §8.2) where
  the matching destroy is easy to forget.
- **Dynamic:** ASan reports `alloc-dealloc-mismatch` (precisely the `new`/`delete[]` family mismatch),
  `new-delete-type-mismatch`, `heap-use-after-free`, and `double-free`, each with allocation and free
  stacks. A `new`/`delete[]` mismatch is one of the easiest real bugs to find this way.

## Offense — value, honest ceiling
- **Ceiling:** the strong ones (double-delete, use-after-delete) are the Lesson-7.5 heap primitives —
  arbitrary alias / arbitrary write, up to code execution, via the same fastbin/tcache mechanics.
  A `new`/`delete[]` mismatch is usually a *controlled* corruption: the pointer handed to `free` is
  offset by the cookie, so it corrupts an adjacent chunk's header or crashes — exploitable in specific
  heap layouts, often just a crash.
- **Floor / obstacles:** same as §7.5 — modern glibc hardening (safe-linking, tcache keys) and ASLR
  mean a lone mismatch typically needs a companion info leak to escalate. The honest statement is
  "this is a UAF/double-free with an 8-byte offset twist," and then the §7.5 analysis applies.

## Defense — and the hook that defines this lesson
- **Make it unrepresentable: don't hand-pair `new`/`delete` at all.** `std::unique_ptr`/`std::vector`/
  `std::string` (Lessons 11–13) own the allocation and call the *matching* deallocation exactly once,
  in the destructor — no manual `delete`, so no mismatch, no double-delete, no leak. `unique_ptr<T[]>`
  even picks `delete[]` for you. This is the "by construction" fix and your book's standing advice
  (`[MEM §1.3.1]`: use smart pointers to manage memory automatically).
- **Match the family mechanically** when you must use raw: `new`↔`delete`, `new[]`↔`delete[]`,
  `malloc`↔`free`, allocator-alloc↔allocator-dealloc; virtual destructor for any base you `delete`
  through.
- **The defender's hook — replace `operator new`/`operator delete` (§8.4).** Because the replacement
  "affects the whole program" `[CPPREF-opnew]`, a few global definitions give you a tap on *every*
  allocation with **no change to application code**:
  - **leak detection** — count live allocations (your exercise); nonzero at exit = leak. This is the
    seed of Lesson 16's detector.
  - **profiling / quotas** — bytes and peak per phase; enforce a cap by throwing `bad_alloc`.
  - **hardening** — a replaced `operator new` can add canaries/guard pages around each object and an
    `operator delete` can poison freed memory and detect double-free with *out-of-line* metadata
    (Lesson 17). This is conceptually what **ASan** does by intercepting the layer below (§8.4 §4).
  The same mechanism an attacker never gets to touch (it's your source) is the cleanest place to *see*
  and *stop* the whole §7.5/§8.5 bug family.

## Through-line to the capstone
Your capstone heap detector is a grown-up version of this lesson's exercise: replace/intercept the
allocation family, keep per-allocation metadata (out of line, with guard pages — Lesson 4.4), and
report UAF/overflow/leak with a Lesson-5 stack unwind. Lesson 8 is where you first stand where the
detector stands — on top of `operator new` — and count. Lessons 16–17 turn counting into catching and
hardening.

## My summary
