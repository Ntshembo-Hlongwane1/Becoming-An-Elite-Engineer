# Lesson 2 — Objects, Pointers & Lifetime in C++

> Status: complete. Exercise: `../../exercises/02-objects-pointers/`.
>
> Lesson 1 said memory is bytes holding numbers. This lesson is about the C++ *abstractions* over
> those bytes: the **object** (a typed, living region of storage), the **pointer** (a value that
> names a byte), and **lifetime** (when it is legal to touch an object). Every dangling-pointer,
> use-after-free, and type-confusion bug — the backbone of real exploits — is a violation of a rule
> in this lesson. We make the rules explicit and let the sanitizers prove them.

Read in order:

1. `01-objects-and-storage.md` — object, storage duration (static/automatic/dynamic/thread),
   when a lifetime begins and ends, `sizeof`, trivially-copyable.
2. `02-pointers-and-references.md` — a pointer *is* an address (Lesson 1 §4); null, `void*`,
   function pointers, pointer-to-pointer `[MEM §2.3.2]`, references, where `const` binds.
3. `03-pointer-arithmetic-and-arrays.md` — array-to-pointer decay, the pointer-arithmetic rules,
   one-past-the-end, `ptrdiff_t`, out-of-bounds UB (ASan).
4. `04-aliasing-and-type-punning.md` — strict aliasing, the type-accessible list, the
   `unsigned char`/`std::byte` exception, the right way to pun (`memcpy`/`bit_cast`), `std::launder`.
5. `05-lifetime-hazards.md` — dangling pointers, use-after-free, use-after-scope, double free,
   `[MEM §1.3.3]`'s dangling example, each caught by ASan.
6. `06-security-view.md` — use-after-free & type confusion as exploit primitives; three hats.
7. `07-glossary.md`.

Then do the exercise: a **bounds-checked `ByteCursor`** over raw storage, plus aliasing-safe
`read_object`/`write_object` — building directly on Lesson 1's codec.

## The one-paragraph picture
An **object** is a region of storage with a type and a **lifetime** that starts when its storage is
obtained and initialized and ends when it is destroyed or its storage is reused. A **pointer** holds
the address of a byte; you may do arithmetic on it only within one array (plus one-past-the-end),
and you may read an object's stored value only through a type the standard calls *type-accessible*
(its own type, or `char`/`unsigned char`/`std::byte`). Touch an object before its lifetime starts
or after it ends, read it through the wrong type, or walk a pointer out of its array, and you have
undefined behaviour — which the optimiser is free to turn into a silently wrong program or an
exploitable one.
