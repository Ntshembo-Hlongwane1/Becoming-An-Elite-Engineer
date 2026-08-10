# 09 — Copy Elision, RVO, and NRVO

> You have spent five docs learning to write copy and move constructors. This doc is about the
> compiler **deleting the calls entirely** — and about the one line people write to "help" that
> makes things measurably worse.
>
> The headline, measured: `return std::move(t)` is **slower** than `return t`, and GCC warns
> about it.

---

## 1. The two kinds of elision, and why the difference matters

C++17 split copy elision into two categories that behave completely differently. The experiment
below separates them by compiling the same program twice, once with
`-fno-elide-constructors`.

```cpp
Tracer nrvo()    { Tracer t("nrvo"); return t; }        // named local
Tracer prvalue() { return Tracer("prvalue"); }          // unnamed temporary
```

**With elision (default):**

```
--- NRVO (named local) ---
  [1] ctor          nrvo
  [1] DTOR          nrvo                 <- ONE object. No copy, no move.
--- prvalue (unnamed temp) ---
  [2] ctor          prvalue
  [2] DTOR          prvalue              <- ONE object.
```

**With `-fno-elide-constructors`:**

```
--- NRVO (named local) ---
  [1] ctor          nrvo
  [2] MOVE ctor  <-  [1] (source now empty)     <- the move reappears
  [1] DTOR          <moved-from>
  [2] DTOR          nrvo
--- prvalue (unnamed temp) ---
  [3] ctor          prvalue
  [3] DTOR          prvalue              <- STILL one object. Unaffected.
```

That contrast is the whole point:

| | Name | C++17 status | Disabled by the flag? |
|---|---|---|---|
| `return Tracer(...)` | **Guaranteed elision** (prvalue) | **mandatory** — part of the language | **no** |
| `return t;` (named local) | **NRVO** | optional optimisation | **yes** |

### Why "guaranteed elision" is not really elision

In C++17 the wording changed fundamentally. A prvalue is no longer a temporary object that gets
copied away — it is an **initialiser** for an object that has not been created yet. The object is
constructed *directly* in its final destination. There is no copy to elide because there was
never a second object.

The practical consequence is large: **this works for types with no copy or move constructor at
all.**

```cpp
struct Immovable {
    Immovable() = default;
    Immovable(const Immovable&) = delete;
    Immovable(Immovable&&)      = delete;
};
Immovable make() { return Immovable{}; }     // legal in C++17. Was impossible in C++14.
Immovable x = make();                        // legal
```

Before C++17 this required an accessible copy or move constructor *even though it was elided*.
That is why C++17 made it mandatory: factories for immovable types became expressible.

NRVO, by contrast, is a genuine optimisation. It is permitted, universally implemented at `-O1`
and above, and **not guaranteed** — so a move constructor must still exist and be accessible,
even if it is never called.

---

## 2. The antipattern: `return std::move(x)`

```cpp
Tracer pessimised() { Tracer t("pess"); return std::move(t); }
```

```
--- return std::move(local) ---
  [3] ctor          pess
  [4] MOVE ctor  <-  [3] (source now empty)     <- a move that did NOT need to happen
  [3] DTOR          <moved-from>
  [4] DTOR          pess
```

Compare the NRVO case: **one** constructor, **one** destructor. Here: two of each, plus a move.

And note the second block of output — with `-fno-elide-constructors` the `std::move` version
produces *identical* output. It was never benefiting from elision at all.

GCC diagnoses it:

```
warning: moving a local object in a return statement prevents copy elision [-Wpessimizing-move]
```

### Why it breaks elision

NRVO requires the returned expression to be the **name of a local variable**. `std::move(t)` is a
function call yielding an xvalue — not a name. The compiler can no longer construct `t` directly
in the caller's storage, because you asked for a specific conversion to be applied. So it
constructs `t` locally, then move-constructs the result.

**You did not help. You forbade the better outcome.**

### `return t;` already moves when it needs to

Since C++11, returning a named local by value triggers **implicit move**: overload resolution
first treats the operand as an rvalue, because the local is about to be destroyed. So:

- NRVO applies → **zero** constructor calls.
- NRVO does not apply → the **move** constructor is chosen automatically.

Either way `return t;` is at least as good as `return std::move(t);` and usually better. The
rule is simple: **return the plain name.**

### Where `std::move` on a return *is* correct

```cpp
// Returning a MEMBER, not a local -- no implicit move applies
std::string Widget::takeName() { return std::move(m_Name); }

// Returning a parameter to a DIFFERENT type -- no elision possible anyway
std::unique_ptr<Base> make() { std::unique_ptr<Derived> d = ...; return std::move(d); }
```

The distinction: implicit move and NRVO apply to **local variables and parameters** being
returned as their own type. A member, or a conversion to a different type, gets neither, and
`std::move` is then doing real work.

---

## 3. When NRVO cannot apply

```cpp
Tracer branchy(bool b) { Tracer x("x"), y("y"); return b ? x : y; }
```

```
--- two candidates, NRVO impossible ---
  [5] ctor          x
  [6] ctor          y
  [7] COPY ctor  <-  [5] x       <- a COPY, not even a move
  [6] DTOR          y
  [5] DTOR          x
  [7] DTOR          x-copy
```

Two things went wrong, and the second is the interesting one.

**NRVO failed** because the compiler must decide *at the point of construction* which object to
build in the caller's storage, and here it cannot know until runtime.

**It fell back to a copy, not a move.** Implicit move applies to a returned expression that
*names* a local variable — an id-expression. `b ? x : y` is a conditional expression, not a
name, so the implicit-move rule does not fire and the lvalue selects the copy constructor.

This is the one case where an explicit `std::move` genuinely helps:

```cpp
return b ? std::move(x) : std::move(y);      // now a move instead of a copy
```

NRVO is impossible either way, so there is no elision to destroy.

### The other blockers

- Returning a **parameter** — NRVO does not apply (implicit move still does, so you get a move).
- Returning a **member** or a global — neither applies.
- Returning **different variables on different paths** — as above.
- Returning a variable also referenced by an out-parameter or captured by reference elsewhere.

**Write your functions with a single return object where you can.** Declaring the result once
and returning it unconditionally gives the compiler its best chance:

```cpp
Tracer better(bool b) {
    Tracer result(b ? "x" : "y");
    return result;                    // NRVO applies
}
```

---

## 4. What this means for the rest of the series

Elision does not make the special members unnecessary — it makes them **cheap to have and rarely
called**.

- **You must still declare them.** NRVO is optional; the move constructor must exist and be
  accessible for the code to compile, even in builds where it is never invoked.
- **`noexcept` still matters.** Elision does not apply to vector reallocation (doc 07). Different
  mechanism, different code path.
- **Factory functions are free.** `PageGuard FetchGuarded(...)` returning by value costs nothing
  — the guard is constructed directly in the caller. That is what makes it reasonable to call on
  every level of every descent, and it is exactly the point `storage/09` §5 made.

> A useful mental model: **the copy/move constructors are a fallback the compiler uses when it
> cannot do better.** Write them correctly, mark the moves `noexcept`, and then structure your
> code so the compiler usually does not need them.

---

## 5. Reproducing this yourself

```bash
g++ -std=c++20 -O2 prog.cpp -o with_elision
g++ -std=c++20 -O2 -fno-elide-constructors prog.cpp -o without_elision
```

`-fno-elide-constructors` is a **diagnostic tool, not a build option**. Use it to see what the
compiler is saving you; never ship with it. Note that it cannot disable the C++17 guaranteed
elision for prvalues, because that is language semantics rather than an optimisation — which is
precisely the distinction §1 draws, and you can see it in the output above.

---

## Checkpoint

- [ ] Reproduce both builds and diff the output
- [ ] Confirm the prvalue case is **identical** in both, and explain why
- [ ] Reproduce the `-Wpessimizing-move` warning; check whether your everyday build flags would
      have shown it to you
- [ ] Write the `Immovable` factory from §1 and confirm it compiles with both copy and move
      deleted
- [ ] Rewrite `branchy` in the single-return-object form and confirm NRVO applies
- [ ] Grep your own code for `return std::move(` and check each one against §2
- [ ] Answer: *why does NRVO require the returned expression to be a name?*
- [ ] Answer: *if elision removes the calls, why must the move constructor still exist?*

Next: [10 — Operator Overloading, Properly](10-operator-overloading.md). You have written
`operator=` four times now; this doc covers the rest of the family and the conventions that keep
them predictable.
