# Lesson 12 — Smart Pointers Inside: `UniquePtr`, `SharedPtr`, `WeakPtr`

> Status: complete. Exercise: `../../exercises/12-smart-pointers/`.
>
> Lesson 11 gave you the rule of five and RAII in the abstract. Smart pointers are that discipline
> crystallised into three small, reusable types that almost every modern C++ program is built from —
> and that make the rule of zero (Lesson 11.1 §4) possible for everyone else. `unique_ptr` is "one
> owner, move it to transfer." `shared_ptr` is "many owners, a reference count frees the object when
> the last one leaves." `weak_ptr` is "look but don't own," the thing that breaks the reference cycle
> that would otherwise leak. People *use* these daily; this lesson has you **build all three from
> scratch** — `UniquePtr`, `SharedPtr` (with a real control block and atomic counts), and `WeakPtr`
> (with `lock()`) — so the reference count, the control block, and the cycle are mechanisms you own,
> not magic. This is also exactly the kind of "show me you understand ownership" question a serious
> C++ interview asks.

This is Phase C, Lesson 2 (ownership). It is the direct application of Lesson 11 (rule of five, move,
exception safety) and uses Lesson 8 (placement new / `operator new`) for the control block.

## Ground rules recap
Every claim is quoted from a cited source, or marked **(derived)** / **(measured)** (a program run on
your VM — GCC 15.2.0). New sources are at the bottom and in `SOURCES.md`.

## Read in order
1. `01-unique-ownership.md` — `unique_ptr`: exclusive ownership, move-only (copy deleted), the rule of
   five reduced to "steal the pointer", `get`/`release`/`reset`, custom deleters, `make_unique`.
2. `02-shared-ownership-and-the-control-block.md` — the **control block** (strong count + weak count +
   the object/deleter); copy ⇒ ++strong, destroy ⇒ --strong, free the object at 0; `make_shared`'s
   single allocation; measured `use_count`.
3. `03-weak-pointers-and-cycles.md` — the reference **cycle leak** (measured: 2 nodes leak) and why
   `weak_ptr` fixes it; `lock()` (alive → `shared_ptr`, expired → empty) and `expired()`; why the
   control block outlives the object.
4. `04-thread-safety-and-costs.md` — what's atomic (the counts) and what isn't (the object); the cost
   of `shared_ptr` vs `unique_ptr`; when each is the right tool; `enable_shared_from_this` in one page.
5. `05-security-view.md` — three hats: use-after-free/double-free as *ownership confusion*, the cycle
   leak as a DoS, and smart pointers as the by-construction fix; where they still bite (raw `.get()`,
   aliasing, `shared` across a trust boundary).
6. `06-glossary.md`.

Then do the exercise: **build `UniquePtr`, `SharedPtr`, `WeakPtr`** — a move-only unique pointer, a
reference-counted shared pointer with a control block and atomic counts, and a weak pointer with
`lock()`/`expired()` that breaks cycles. Tests cover ownership transfer, `use_count`, exactly-once
destruction, `lock()` before/after expiry, and a broken cycle — all under ASan+UBSan.

## The one-paragraph picture
A **`unique_ptr`** is just a raw pointer wrapped in an RAII type whose destructor `delete`s it and
whose copy operations are **deleted** — so there is exactly one owner, and ownership moves (never
copies) with `std::move` (the rule of five from Lesson 11, where "copy" is forbidden and "move" steals
the pointer). A **`shared_ptr`** adds a separately-allocated **control block** holding two atomic
counters — a **strong count** (how many `shared_ptr`s own the object) and a **weak count** (how many
`weak_ptr`s watch it) — plus the object (or a pointer to it) and its deleter; copying a `shared_ptr`
increments the strong count, destroying one decrements it, and when the strong count hits zero the
**object** is destroyed, while the control block itself lives until the weak count also hits zero. A
**`weak_ptr`** holds the control block but not a strong count, so it never keeps the object alive; to
use the object you call `lock()`, which atomically returns a `shared_ptr` if the strong count is still
nonzero and an empty one if the object has already been destroyed. That last piece is what breaks the
**cycle**: two objects that `shared_ptr` each other keep both strong counts at 1 forever and leak;
making one edge a `weak_ptr` lets the counts reach zero. Everything here is Lesson 11's ownership
rules applied to a pointer, plus one counter.

## New sources introduced here (also appended to `SOURCES.md`)
| Key | Source |
|---|---|
| `[CPPREF-shared]` | cppreference, `std::shared_ptr` — shared ownership, the control block, thread safety. https://en.cppreference.com/w/cpp/memory/shared_ptr |
| `[CPPREF-weak]` | cppreference, `std::weak_ptr` — non-owning, `lock()`, `expired()`, breaking cycles. https://en.cppreference.com/w/cpp/memory/weak_ptr |
| `[CPPREF-unique]` | cppreference, `std::unique_ptr` — exclusive ownership, move-only, custom deleter. https://en.cppreference.com/w/cpp/memory/unique_ptr |
| `[CPPREF-make_shared]` | cppreference, `std::make_shared` — single allocation for object + control block. https://en.cppreference.com/w/cpp/memory/shared_ptr/make_shared |
| `[CPPREF-esft]` | cppreference, `std::enable_shared_from_this`. https://en.cppreference.com/w/cpp/memory/enable_shared_from_this |
