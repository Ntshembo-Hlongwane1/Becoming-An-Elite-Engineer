# Lesson 11 — RAII, the Rule of 0/3/5, Move Semantics, and Exception Safety

> Status: complete. Exercise: `../../exercises/11-vector/`.
>
> Phases A and B answered "what is memory" and "who hands it out". Phase C answers the question that
> actually keeps programs correct: **who owns this allocation, and who frees it — exactly once, even
> when an exception is thrown halfway through?** The C++ answer is **RAII**: tie every resource to an
> object's lifetime, so the destructor frees it automatically (Lessons 7–8 gave you the raw
> allocate/free; RAII makes them impossible to leak or double-free). But the moment an object *owns* a
> resource, copying and moving it stop being free — a naïve copy double-frees, a naïve move dangles.
> The **rule of 0/3/5** says which special member functions you then owe, **move semantics** make
> transfer cheap, and the **exception-safety guarantees** say what state you're left in when something
> throws. This lesson is where you stop *using* `std::vector` and *become the person who could have
> written it* — which is exactly the jump from "C++ programmer" to the systems/C++ roles you're after.

This is Phase C, Lesson 1 (ownership). It builds on Lesson 8 (placement new, `operator new`), Lesson 9
(alignment), and Lesson 10 (allocators). It is the prerequisite for Lesson 12 (smart pointers) and
Lesson 13 (containers).

## Ground rules recap
Every claim is quoted from a cited source, or marked **(derived)** / **(measured)** (a program run on
your VM — GCC 15.2.0). One of the book's own examples in this area is **wrong** (`return std::move(x)`);
spotting and fixing it (§11.3) is part of the training. New sources are at the bottom and in `SOURCES.md`.

## Read in order
1. `01-raii-and-the-rule-of-zero.md` — RAII: resources tied to object lifetime; the destructor frees;
   why it survives exceptions; the **rule of zero** (prefer owning *members* so you write none of
   this).
2. `02-copy-and-the-rule-of-three.md` — copy constructor & copy assignment; shallow vs deep copy; the
   double-free a defaulted copy causes on an owning type; self-assignment; the **rule of three**.
3. `03-move-and-the-rule-of-five.md` — rvalue references, `std::move` (just a cast), move ctor/assign
   that *steal* and leave a valid-but-unspecified source; `noexcept`; the **rule of five**; and the
   book's `return std::move(x)` anti-pattern vs NRVO (measured).
4. `04-exception-safety-and-the-strong-guarantee.md` — the four guarantees (nothrow/strong/basic/none);
   copy-and-swap; the `std::vector` reallocation crux and `std::move_if_noexcept` (measured: noexcept
   move → 7 moves, throwing move → 7 copies; vector unchanged after a throw).
5. `05-security-view.md` — three hats: double-free / UAF / leak as *broken ownership*, and RAII as the
   fix that makes them unrepresentable.
6. `06-glossary.md`.

Then do the exercise: **a `Vector<T>` with the strong guarantee** — an owning dynamic array built on
Lesson 8's placement new and Lesson 9's alignment, with a correct rule of five, `reserve`/`emplace_back`
growth that preserves the strong guarantee via `move_if_noexcept`, and copy-and-swap assignment.
Tested (value semantics, move, growth, a *throwing* element that must leave the vector intact,
move-only element types, leak-freedom) under ASan+UBSan.

## The one-paragraph picture
**RAII** makes an object the sole owner of a resource: the constructor acquires it, the destructor
releases it, and because C++ runs destructors automatically at end of scope — *including while an
exception unwinds the stack* — the resource is freed exactly once on every path, with no manual
`free`/`delete` to forget. The price is that an owning object can't be copied or moved bit-for-bit: a
bitwise copy makes two owners of one allocation (→ double-free), so if you write a **destructor** you
almost certainly also need a **copy constructor** and **copy assignment** that *deep-copy* (the **rule
of three**), and, since C++11, a **move constructor** and **move assignment** that *steal* the
allocation and null out the source for speed (the **rule of five**). `std::move` is just a cast that
says "you may steal from me." Move operations should be `noexcept`, because containers like `vector`
will only *move* your objects during reallocation if moving can't throw — otherwise they *copy*
(`std::move_if_noexcept`), so that if something throws mid-reallocation the original is still intact:
the **strong exception guarantee** (the operation either fully succeeds or has no effect). The whole
lesson is the discipline that makes "owns a resource" and "exception-safe" the same habit — and the
**rule of zero** is the reward: build from members that already obey it (`vector`, `string`,
`unique_ptr`) and you write none of the five yourself.

## New sources introduced here (also appended to `SOURCES.md`)
| Key | Source |
|---|---|
| `[CPPREF-rule]` | cppreference, *The rule of three/five/zero*. https://en.cppreference.com/w/cpp/language/rule_of_three |
| `[CPPREF-move-ctor]` | cppreference, *Move constructors* — steal + valid-but-unspecified; noexcept & vector. https://en.cppreference.com/w/cpp/language/move_constructor |
| `[CPPREF-move_if_noexcept]` | cppreference, `std::move_if_noexcept` — move vs copy for the strong guarantee. https://en.cppreference.com/w/cpp/utility/move_if_noexcept |
| `[CPPREF-copy-elision]` | cppreference, *Copy elision* / NRVO — why `return std::move(x)` pessimizes. https://en.cppreference.com/w/cpp/language/copy_elision |
| `[CORE-C.21]` | C++ Core Guidelines C.21 — "if you define or =delete any copy, move, or destructor, define or =delete them all." https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#c21 |
