# The Rule of Five — Special Member Functions, Ownership, and Operators in C++

> **Goal:** finish this series able to look at any class and answer, without hesitating: *does
> this type need special members? which ones? what does the compiler generate if I say nothing?
> what breaks if I get it wrong?* — and to know **why**, at the level of what the machine
> actually does.
>
> **Everything here is measured.** Every claim in these docs is backed by a program I compiled
> and ran on your toolchain (GCC 16, C++20, MinGW-w64). Where the output is surprising — and
> several are — the real output is printed. Nothing is asserted from memory.
>
> **This is not a reference.** It is an argument, built in order. Doc 01 breaks a program and
> shows you the crash; every doc after that is a response to a problem the previous one left
> open.

---

## Why you are reading this

You wrote `PageGuard` in `lesson doc/storage/08-page-guards.md`:

```cpp
PageGuard(const PageGuard&)            = delete;
PageGuard& operator=(const PageGuard&) = delete;
PageGuard(PageGuard&& other) noexcept;
PageGuard& operator=(PageGuard&& other) noexcept;
~PageGuard();
```

Five declarations. That doc explained *what each one does here*. This series explains **the
system they belong to** — why five and not three, why `noexcept` is load-bearing rather than
decorative, what the compiler would have written if you had said nothing, and why one of those
five silently disables two others.

Here is the finding that motivates the whole series, from doc 06 §2. Two structs, identical
except that one has an **empty destructor that does nothing**:

```cpp
struct A { Member m; };                  //  ->  member MOVED
struct B { Member m; ~B(){} };           //  ->  member COPIED
```

```
A  (rule of zero, nothing declared):
      member MOVED
B  (user-declared DESTRUCTOR only):
      member COPIED
```

Adding `~B(){}` turned every move into a copy, across the entire program, silently, with no
warning at any optimisation level. On a type holding a megabyte buffer that is a catastrophic
regression, and nothing in the language will tell you.

**That is why this series exists.**

---

## The series

| Doc | Title | The question it answers |
|---|---|---|
| [01](01-the-problem.md) | The Problem: Resources & Ownership | Why does C++ need any of this? *(with a real double-free crash)* |
| [02](02-lifetime-and-destructor.md) | Object Lifetime & the Destructor | When exactly does a destructor run, and in what order? |
| [03](03-value-categories.md) | Value Categories | What *is* an lvalue? What is a prvalue, an xvalue? |
| [04](04-copy-semantics.md) | Copy Semantics | Copy ctor vs copy assignment; deep vs shallow; self-assignment |
| [05](05-move-semantics.md) | Move Semantics | What a move actually is; what "moved-from" means |
| [06](06-the-rules.md) | The Rules of 0, 3, and 5 | Which declarations suppress which — *the suppression matrix* |
| [07](07-noexcept.md) | `noexcept` and Why Containers Care | Measured: 0 copies vs 15 copies from one keyword |
| [08](08-exception-safety.md) | Exception Safety & Copy-and-Swap | The three guarantees; one idiom that gives you two of them free |
| [09](09-copy-elision.md) | Copy Elision, RVO, NRVO | Why `return std::move(x)` is *slower* |
| [10](10-operator-overloading.md) | Operator Overloading, Properly | Canonical forms, member vs free, ref-qualifiers, `<=>` |
| [11](11-case-studies.md) | Case Studies | `PageGuard`, `DiskManager`, `BPlusTree`, `unique_ptr`, a full `String` |
| [12](12-checklist.md) | Decision Checklist & Antipatterns | A flowchart, the bug catalogue, and a reusable test harness |

---

## The five (really six) special member functions

Every class has these. You either write them, default them, delete them, or let the compiler
decide — but they always exist as a question.

```cpp
class T {
    T();                             // 0. default constructor  (not part of "the five")
    ~T();                            // 1. destructor
    T(const T&);                     // 2. copy constructor
    T& operator=(const T&);          // 3. copy assignment
    T(T&&) noexcept;                 // 4. move constructor
    T& operator=(T&&) noexcept;      // 5. move assignment
};
```

Three questions to hold in your head throughout, because almost every subtlety in these docs is
one of them:

1. **Construction or assignment?** Construction builds a *new* object on raw memory. Assignment
   overwrites an *existing* one, which must first dispose of what it already owns. Different
   jobs, different code.
2. **Copy or move?** A copy leaves the source usable. A move may gut it. Which one runs is
   decided by the **value category** of the source (doc 03), not by what you meant.
3. **Declared, defaulted, deleted, or implicit?** Four states, and they interact — doc 06's
   matrix. Declaring one can silently delete another.

---

## How to read this

**In order, running the code.** Every doc has programs you should compile and run; the output
in the docs is what mine printed, and if yours differs that is a finding worth chasing.

The instrumented `Tracer` class from doc 02 is used throughout — build it once and keep it. It
prints a line on every construction, destruction, copy, and move, which turns all of this from
rules-to-memorise into behaviour you can watch.

```bash
g++ -std=c++20 -O2 -Wall -Wextra prog.cpp -o prog && ./prog
```

> **A note on `-O2`.** Optimisation level changes observable behaviour here, because copy
> elision (doc 09) removes constructor calls entirely. Where it matters, the docs say which
> flags produced the output, and doc 09 shows the difference with `-fno-elide-constructors`.

## Prerequisites

- `lesson doc/storage/08-page-guards.md` §3.1 — the short version of this series. Doc 11 here
  revisits that class in full.
- `lesson doc/storage/02-the-page.md` §5 — trivially-copyable types, which are exactly the ones
  that need *none* of this.
- Your `BPlusTree.hpp` and `DiskManager` — both are case studies in doc 11.

## Where this lands in `searchengine`

Nowhere directly — this series produces understanding, not a component. But it audits real code
you have already written, and doc 12's checklist is meant to be applied to every class in
`internal/kernal/` as you write it.
