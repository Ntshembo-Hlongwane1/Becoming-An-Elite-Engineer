# 02 — Object Lifetime and the Destructor

> **Why this comes second.** Doc 01 crashed because "both destructors ran." Before you can
> reason about *what* a destructor should do, you need to know exactly **when** it runs — and
> the answer is more precise, and more useful, than "when the object goes away."
>
> **Build target:** the `Tracer` class, which you will use for the rest of the series. It prints
> a line on every construction, destruction, copy, and move, turning invisible compiler
> decisions into visible output.

---

## 1. Storage duration — the four lifetimes

An object's **storage duration** determines when it is created and destroyed. There are four,
and only the first two matter much here.

| Duration | Created | Destroyed | Declared as |
|---|---|---|---|
| **Automatic** | at its declaration | at end of enclosing scope | a local variable |
| **Dynamic** | at `new` | at `delete` — *only* | `new T` |
| **Static** | before `main` (or on first use) | after `main` returns | `static`, globals |
| **Thread** | at thread start | at thread exit | `thread_local` |

The critical distinction for everything that follows:

```
Automatic:  the compiler guarantees destruction.  You cannot leak.
Dynamic:    YOU guarantee destruction.            You can leak, double-free, and dangle.
```

RAII — the technique doc 08 of the storage series is built on — is precisely the trick of
**wrapping a dynamic-duration resource inside an automatic-duration object**, so the compiler's
guarantee covers your resource. `PageGuard` is an automatic object whose destructor releases a
pin; that is the whole idea in one sentence.

The proof that dynamic duration ignores scope:

```
=== E. dynamic storage: destructor runs on delete, not scope exit ===
    ctor heap
  scope ending WITHOUT delete -> no dtor, leaked
    dtor heap
  (deleted explicitly)
```

The scope ended and nothing happened. `delete` is what ran the destructor. Remove the `delete`
and that object is never destroyed at all.

---

## 2. Construction and destruction order

Destruction is always **exactly the reverse** of construction. Measured:

```
=== A. member + base construction/destruction ORDER ===
    ctor Base::member          <- 1. base's members
  Base ctor body               <- 2. base's constructor body
    ctor Derived::m1           <- 3. derived's members, in DECLARATION order
    ctor Derived::m2
    ctor Derived::m3
  Derived ctor body            <- 4. derived's constructor body

  Derived dtor body            <- 4'. derived's destructor body
    dtor Derived::m3           <- 3'. members in REVERSE declaration order
    dtor Derived::m2
    dtor Derived::m1
  Base dtor body               <- 2'.
    dtor Base::member          <- 1'.
```

Four rules fall out, and each has a practical consequence:

**Members are constructed in declaration order, not initialiser-list order.** If you write
`Derived() : m3(...), m1(...)` the compiler still builds `m1` first, and warns
(`-Wreorder`). A member initialised from another member reads garbage if it is declared first.

**Members are destroyed in reverse declaration order.** So a member can safely use an
earlier-declared member in its destructor — the earlier one is still alive.

**Base before derived, on the way in; derived before base, on the way out.** By the time
`~Base` runs, the derived part is already gone. This is why calling a virtual function from a
destructor does *not* dispatch to the derived override — the derived object no longer exists.

**The constructor body runs last.** Everything in the member-initialiser list is already done.
Assigning members in the body is therefore *assignment*, not initialisation — you constructed
them and then overwrote them. For a `std::string` member that is a wasted allocation; for a
`const` or reference member it does not compile at all.

> **Partial construction is handled correctly.** If `m2`'s constructor throws, `m1` and the
> base are destroyed, `m3` is never constructed, and — crucially — **`~Derived` does not run**,
> because no `Derived` was ever completed. The rule: *a destructor runs only for an object whose
> constructor completed.* This is why constructors that acquire two resources are dangerous, and
> why each resource wants its own RAII member.

---

## 3. Temporaries: the end of the full expression

```
=== B. temporary lifetime: end of full expression ===
  before
    ctor temporary
    dtor temporary        <- destroyed at the semicolon
  after the semicolon
```

A temporary lives until the end of the **full expression** that created it — informally, the
semicolon. Not the end of the statement's line, not the end of the scope.

This produces one of C++'s most common dangling-pointer bugs:

```cpp
const char* p = std::string("hello").c_str();   // temporary dies at the ';'
printf("%s", p);                                 // p dangles. UB.
```

And a subtler one relevant to your storage engine:

```cpp
NodePage n = NodePage(*pool.FetchGuarded(id).Get());   // guard dies at the ';'
n.KeyAt(0);                                             // page may already be evicted
```

The guard is a temporary. It unpins at the semicolon. `n` then holds a reference into a frame
that is once again evictable — exactly the hazard `storage/05` §6 warned about for non-owning
views.

### Lifetime extension

Binding a temporary to a `const` reference (or an rvalue reference) extends its life to the
reference's scope:

```
=== C. lifetime EXTENSION by binding to const ref ===
    ctor temporary
  temp still alive here, name=temporary
  leaving scope
    dtor temporary        <- extended to end of scope
```

```cpp
const N& r = makeTemp();       // lives as long as r
```

Two hard limits worth knowing, because both look like they should work:

- **It does not chain through a function return.** Returning a `const T&` that binds a local
  temporary gives you a dangling reference; extension applies only at the point of binding.
- **It does not apply to a reference *member*.** `struct S { const N& r; };` initialised from a
  temporary leaves `r` dangling once the constructor returns. This is a real trap for the
  view-style classes in your engine.

---

## 4. Stack unwinding — the guarantee RAII rests on

```
=== D. stack unwinding runs destructors ===
    ctor in-try
    ctor inside-thrower
    dtor inside-thrower      <- innermost first
    dtor in-try
  caught: boom
```

When an exception propagates, every fully-constructed automatic object between the `throw` and
the matching `catch` is destroyed, innermost scope first. Nothing is skipped.

**This is the entire reason RAII works.** It is what makes `PageGuard` correct rather than
merely convenient — `storage/08` §1 showed a hand-written `UnpinPage` being skipped by a
`throw`, and this is the mechanism that makes the guard immune.

### Why destructors must not throw

If a destructor throws *during* unwinding, there are two exceptions in flight and the language
cannot choose. It calls `std::terminate`. No `catch` runs, no cleanup, process gone.

Since C++11, destructors are therefore **implicitly `noexcept`** — a destructor that lets an
exception escape calls `std::terminate` immediately rather than propagating. That is why
`~DiskManager` swallows errors:

```cpp
~DiskManager() {
    if (m_File) { std::fflush(m_File); std::fclose(m_File); }   // errors deliberately ignored
}
```

You may `try/catch` *inside* a destructor. You may not let anything out.

---

## 5. The `Tracer` — build this now

Every remaining doc uses it. It is the single most effective tool for learning this material,
because it makes the compiler's invisible choices visible.

```cpp
// lesson doc/rule-of-five/tracer.hpp
#pragma once
#include <cstdio>
#include <string>
#include <utility>

struct Tracer {
    std::string name;
    int id;
    static inline int counter = 0;

    Tracer(std::string n = "?") : name(std::move(n)), id(++counter) {
        printf("  [%d] ctor          %s\n", id, name.c_str());
    }
    ~Tracer() {
        printf("  [%d] DTOR          %s\n", id, name.c_str());
    }
    Tracer(const Tracer& o) : name(o.name + "-copy"), id(++counter) {
        printf("  [%d] COPY ctor  <-  [%d] %s\n", id, o.id, o.name.c_str());
    }
    Tracer& operator=(const Tracer& o) {
        printf("  [%d] COPY assign <- [%d] %s\n", id, o.id, o.name.c_str());
        name = o.name + "-copyassigned";
        return *this;
    }
    Tracer(Tracer&& o) noexcept : name(std::move(o.name)), id(++counter) {
        printf("  [%d] MOVE ctor  <-  [%d] (source now empty)\n", id, o.id);
        o.name = "<moved-from>";
    }
    Tracer& operator=(Tracer&& o) noexcept {
        printf("  [%d] MOVE assign <- [%d]\n", id, o.id);
        name = std::move(o.name);
        o.name = "<moved-from>";
        return *this;
    }
};
```

Three design notes, since this class is itself an example of the material:

- **`static inline int counter`** — `inline` on a static data member (C++17) lets it be defined
  in a header without a separate `.cpp` definition. Same ODR mechanism as the
  `inline constexpr` in `storage/02` §1.
- **Each object gets a unique `id`**, so you can follow individual objects through copies and
  moves. Without it the output is unreadable.
- **The move operations rename the source `<moved-from>`**, which makes doc 05's central
  question — *what state is a moved-from object in?* — directly observable.

---

## 6. What a destructor is actually for

Given all of the above, the destructor's job is narrow and precise:

> **Release exactly the resources this object owns, and nothing else.**

Not "clean up." Not "reset the object" — the object is about to stop existing, so zeroing its
members is wasted work (though it can help debugging). Not "notify anyone."

A destructor should be:

- **Idempotent-safe by construction.** It runs exactly once per completed object; you do not
  need to guard against re-entry.
- **Non-throwing.** §4.
- **Fast.** It runs on every scope exit, including exceptional ones.
- **Complete.** Every resource acquired must be released here, or by a member's destructor.

The last point is the argument for the Rule of Zero: **if every member releases itself, your
destructor has nothing to do and you should not write one at all.** Doc 06 shows that writing
an empty one is not free — it silently disables your move operations.

---

## Checkpoint

- [ ] `tracer.hpp` saved somewhere you can `#include` from the rest of the series
- [ ] Reproduce all five sections of the experiment above; confirm your output matches
- [ ] Add a member to `Derived` between `m1` and `m2` and predict the destruction order before
      running it
- [ ] Make `m2`'s constructor throw. Confirm `~Derived` does **not** run, but `~Base` and
      `~m1` do
- [ ] Write the `std::string(...).c_str()` dangling bug and run it under `-O2`. Note that it
      often *appears* to work — that is what makes it dangerous
- [ ] Answer: *why is a destructor implicitly `noexcept`?*
- [ ] Answer: *why does assigning a member in the constructor body differ from initialising it
      in the initialiser list?*

Next: [03 — Value Categories](03-value-categories.md). Before you can understand why a copy
happens in one place and a move in another, you need to know what the compiler is looking at
when it chooses.
