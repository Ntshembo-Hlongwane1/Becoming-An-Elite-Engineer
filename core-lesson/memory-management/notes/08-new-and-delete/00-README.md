# Lesson 8 — `new` / `delete` for Real (placement new, replacement)

> Status: complete. Exercise: `../../exercises/08-new-delete/`.
>
> Lesson 7 was `malloc`: raw bytes, no types. C++ almost never calls `malloc` directly — it uses
> `new`. This lesson shows that `new` is **not** a primitive: a `new` expression is *two* operations
> glued together — call an **allocation function** (`operator new`, which is essentially `malloc`
> from Lesson 7) to get raw storage, then run a **constructor** in that storage. `delete` is the same
> in reverse: destructor, then deallocation function. Pull those two apart and three things you've
> been using on faith become mechanisms you control: **placement new** (construct at an address you
> already own), **replacing `operator new`** program-wide (the hook every leak detector and sanitizer
> uses), and the exact rules that make "mismatched `new`/`delete[]`" undefined behaviour.

This is Phase B, Lesson 2 (allocation). It builds on Lesson 7 (`malloc`, chunks, alignment) and
Lessons 1–2 (objects & lifetime). It is the direct prerequisite for Lessons 10–13 (custom allocators,
smart pointers, containers), all of which are placement-new over an `operator new`.

## Ground rules recap
Every claim is quoted from a cited source, or marked **(derived)** / **(measured)** (a program run on
your VM — GCC 15.2.0 / glibc 2.43). New sources for this lesson are listed at the bottom and appended
to `../../SOURCES.md`.

## Read in order
1. `01-the-new-expression.md` — `new` = allocate **then** construct; `delete` = destruct **then**
   deallocate; `new[]`/`delete[]` and the array **cookie**; why mismatching the forms is UB.
   Measured: `new Noisy` prints `[operator new]` *before* the constructor.
2. `02-operator-new-and-delete.md` — the allocation functions themselves: the whole replaceable
   family (throwing / nothrow / aligned / array), `std::bad_alloc`, the new-handler, class-specific
   `operator new`, and the alignment guarantee (`__STDCPP_DEFAULT_NEW_ALIGNMENT__` = 16 here).
3. `03-placement-new.md` — construct an object at an address you already own; the `<new>` header; the
   mandatory **manual destructor call**; `std::construct_at`/`std::destroy_at` (C++20); why every
   container and arena is built on this.
4. `04-replacing-global-operator-new.md` — replace `operator new`/`delete` for the whole program;
   the matched set you must provide; counting with a size header (a callback to Lesson 7); the three
   pitfalls (recursion, calls before `main`, sized delete); how this *is* your exercise.
5. `05-security-view.md` — three hats: `new`/`delete[]` mismatch and double-delete as corruption, and
   `operator new` replacement as the defensive **hook** behind leak detectors and ASan.
6. `06-glossary.md`.

Then do the exercise: **an allocation-counting global `operator new`** — replace the global
allocation/deallocation family so every `new`/`delete` in the program is counted (live allocations,
total allocations, bytes, peak), using a size header exactly like Lesson 7's chunk header. The tests
drive real `new`/`delete`/`new[]`/`delete[]` and assert the counters move correctly, under ASan+UBSan.

## The one-paragraph picture
`T* p = new T(args)` is shorthand the compiler expands into roughly: `void* raw = operator new(sizeof(T));
T* p = new(raw) T(args);` — first an **allocation function** call (`operator new`, a replaceable
function that by default just wraps `malloc`, Lesson 7), then **placement new**, which runs `T`'s
constructor in that raw storage and yields a typed pointer. `delete p` is the mirror: `p->~T();
operator delete(p);` — destructor first, then the **deallocation function**. Everything else in the
lesson follows from separating those halves: `operator new`/`operator delete` are ordinary functions
you may **replace** for the whole program (so you can count or harden every allocation); **placement
new** is the allocate-less half you call directly to build objects in storage you already have (the
foundation of `std::vector`, arenas, and `std::optional`); and the form you allocate with (`new` vs
`new[]`, which operator, which alignment) must be matched *exactly* by the form you free with, because
each form may stash different bookkeeping — an **array cookie** for `new[]`, a different operator for
over-aligned types — that only the matching `delete` knows how to undo.

## New sources introduced here (also appended to `SOURCES.md`)
| Key | Source |
|---|---|
| `[CPPREF-new]` | cppreference, *new expression* — two steps (allocate+construct), placement new, ctor-throw cleanup, array cookie. https://en.cppreference.com/w/cpp/language/new |
| `[CPPREF-opnew]` | cppreference, *operator new* — replaceable family, throwing vs nothrow, replacement affects whole program, alignment. https://en.cppreference.com/w/cpp/memory/new/operator_new |
| `[CPPREF-opdelete]` | cppreference, *operator delete* — deallocation family, sized delete, noexcept. https://en.cppreference.com/w/cpp/memory/new/operator_delete |
| `[CPPREF-construct_at]` | cppreference, `std::construct_at` / `std::destroy_at` (C++20). https://en.cppreference.com/w/cpp/memory/construct_at |
| `[CPPREF-replacement]` | cppreference, *replacement functions* — rules for replacing the global operator new/delete. https://en.cppreference.com/w/cpp/language/replacement_function |
