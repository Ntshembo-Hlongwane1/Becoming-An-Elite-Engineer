# 03 — Value Categories

> **Why you need this before move semantics.** "A move happens when you pass an rvalue" is the
> usual explanation, and it is useless until you can look at an expression and say what it is.
> This doc makes that mechanical.
>
> The payoff is one specific fact that catches nearly everyone, and that you will hit the first
> time you write a move constructor: **a named rvalue reference is an lvalue.** §5 proves it.

---

## 1. Value categories are properties of *expressions*, not types

This is the misconception to clear first.

```cpp
W a;
```

`a` is a variable of type `W`. But `a` *as an expression* has a value category: it is an
**lvalue**. And `std::move(a)` — the same object — is an **xvalue**. Same object, same type,
different category, because category describes the *expression*, not the thing.

Every expression in C++ has exactly one type and exactly one value category. Overload resolution
uses both.

---

## 2. The taxonomy

C++11 split the old lvalue/rvalue pair along two independent questions:

- **Does it have identity?** Can you take its address / does it name a persistent location?
- **Can it be moved from?** Is it safe to gut it, because nobody will look at it again?

```
                        expression
                       /          \
                 glvalue          rvalue
                (has identity)   (movable)
                /        \       /        \
           lvalue        xvalue          prvalue
        identity,      identity,        no identity,
        not movable    movable          movable
```

| Category | Identity | Movable | Examples |
|---|---|---|---|
| **lvalue** | yes | no | `a`, `arr[0]`, `obj.member`, `*ptr`, `"literal"` |
| **xvalue** | yes | yes | `std::move(a)`, `static_cast<W&&>(a)` |
| **prvalue** | no | yes | `42`, `makeW()`, `W{}`, `a + b` |

The two composite names are just unions:

- **glvalue** = lvalue ∪ xvalue — "has identity"
- **rvalue** = xvalue ∪ prvalue — "can be moved from"

**In practice you mostly need one distinction: lvalue vs rvalue.** The xvalue/prvalue split
matters only for copy elision (doc 09), where prvalues get a guarantee xvalues do not.

> The mnemonic that actually helps: **"eXpiring value"** for xvalue (it still exists, but is
> about to die), and **"Pure rvalue"** for prvalue (a value that never had a home).

---

## 3. Measured: what each expression actually is

```cpp
#define CAT(e) /* uses decltype((e)) to report the category */
```

Real output:

```
  a                      : lvalue
  c                      : lvalue            (const doesn't change the category)
  makeW()                : prvalue (rvalue)
  std::move(a)           : xvalue (rvalue)
  a.v                    : lvalue            (member of an lvalue)
  42                     : prvalue (rvalue)
  arr[0]                 : lvalue
  "literal"              : lvalue            <-- surprising
```

**String literals are lvalues.** A string literal is an array of `const char` with static
storage duration — it has an address that outlives every expression. That is why
`const char* p = "hello";` is safe forever while `const char* p = std::string("hello").c_str();`
dangles at the semicolon (doc 02 §3). Different categories, and the difference is the bug.

The trick used here is worth keeping: `decltype((e))` — **with double parentheses** — yields
`T&` for an lvalue, `T&&` for an xvalue, and plain `T` for a prvalue. Single parentheses
`decltype(e)` gives the declared type instead, which is a different question.

---

## 4. How overload resolution chooses

Given three overloads, here is what actually binds:

```cpp
void f(W&);          // non-const lvalue ref
void f(const W&);    // const lvalue ref  -- binds almost anything
void f(W&&);         // rvalue ref
```

```
f(a)                    -> f(W&)          non-const lvalue
f(c)                    -> f(const W&)    const lvalue
f(makeW())              -> f(W&&)         prvalue
f(makeConstW())         -> f(const W&)    <-- a CONST rvalue!
f(std::move(a))         -> f(W&&)         xvalue
f(W{})                  -> f(W&&)         prvalue
f(arr[0])               -> f(W&)          lvalue
f(a.v ? a : a)          -> f(W&)          lvalue
```

The binding rules, in priority order:

| Parameter | Binds to |
|---|---|
| `T&` | non-const lvalues only |
| `const T&` | **everything** — lvalues, rvalues, const or not |
| `T&&` | non-const rvalues only |

Two consequences worth internalising:

**`const T&` is the universal fallback.** It binds to anything, which is why a class with only
a copy constructor still compiles when you `std::move` it — the copy constructor takes
`const T&`, which happily accepts the rvalue. **`std::move` on a type with no move constructor
silently copies.** No error, no warning. Doc 06 §2 shows this costing real performance.

**`f(makeConstW())` picks `const W&`, not `W&&`.** A `const` rvalue cannot bind to `W&&`,
because a move constructor needs to *modify* its source (to empty it) and `const` forbids that.
So **`const` silently disables moves.** This is why you should not return `const` by value, a
habit that was once recommended and is now actively harmful:

```cpp
const W makeConstW();     // every "move" from this is a copy
W       makeW();          // correct
```

---

## 5. The gotcha: a named rvalue reference is an lvalue

This is the one that catches everyone, and it is directly relevant to writing move constructors.

```cpp
void sink(W&& p) {
    f(p);                 // which overload?
}
```

Measured:

```
    inside sink(W&&), calling f(p):              -> f(W&)   non-const lvalue ref
    inside sink, calling f(std::move(p)):        -> f(W&&)  rvalue ref
```

`p` is *declared* as `W&&`. But **`p` as an expression is an lvalue** — it has a name, it has an
address, you can take it, and you can use it twice. The type says "I was passed something
movable"; the category says "but I am a variable now."

The rule: **if it has a name, it is an lvalue.** Regardless of its declared type.

### Why the language does this, and why it is right

Safety. Consider:

```cpp
void process(W&& p) {
    helper(p);            // if this moved from p...
    finalise(p);          // ...this would see a gutted object
}
```

If a named `W&&` were an rvalue, the first call would silently move out of `p` and the second
would operate on wreckage. By making it an lvalue, C++ requires you to say `std::move(p)`
**explicitly, at the exact point you are done with it** — which is also the point a reader can
see the ownership transfer.

### The consequence for every move constructor you write

```cpp
Tracer(Tracer&& o) noexcept
    : name(std::move(o.name))    // WITHOUT std::move this COPIES the string
{ }
```

`o` is a named rvalue reference, so `o.name` is an lvalue, so `name(o.name)` calls
`std::string`'s **copy** constructor. Your move constructor would compile, run, produce correct
results, and be as slow as a copy. Nothing warns.

**Every member access inside a move constructor needs `std::move`.** This is the single most
common bug in hand-written move operations. Doc 12 §3 has a test that catches it.

---

## 6. Where each category comes from

A quick reference for reading unfamiliar code:

**lvalues**
- variables, function parameters, data members: `a`, `p`, `obj.m`
- `*ptr`, `arr[i]`
- functions returning `T&`: `v[0]`, `*it`, `os << x`
- string literals
- pre-increment: `++i`
- assignment: `a = b` (yields an lvalue — which is why `(a = b) = c` compiles)

**prvalues**
- literals except strings: `42`, `true`, `nullptr`, `3.14`
- functions returning by value: `makeW()`
- arithmetic and comparison: `a + b`, `a < b`
- `T{}`, `T(args)`
- lambda expressions
- post-increment: `i++`

**xvalues**
- `std::move(x)`, `static_cast<T&&>(x)`
- functions returning `T&&`
- member access on an xvalue: `std::move(obj).m`

> The `i++` vs `++i` split is a nice consistency check on your understanding. `++i` returns a
> reference to the incremented object — an lvalue. `i++` must return the *old* value, which no
> longer lives anywhere, so it returns a copy — a prvalue. That is also exactly why `i++` is
> more expensive for non-trivial types, and why `++it` is the convention in loops.

---

## 7. Bringing it back to the five

Value category is **the input** to the decision the rest of this series is about:

```
        expression is an lvalue   ->   copy constructor / copy assignment
        expression is an rvalue   ->   move constructor / move assignment
                                        (falling back to copy if no move exists)
```

That is the entire dispatch rule. Everything else — `std::move`, `noexcept`, elision — is about
controlling or exploiting which side of that line an expression lands on.

`std::move` does exactly one thing: **it casts an lvalue to an xvalue** so the second row is
chosen. Doc 05 §2 shows its one-line implementation.

---

## Checkpoint

- [ ] Reproduce the `CAT` output; add `CAT(a = c)` and `CAT(a.v++)` and explain both
- [ ] Reproduce the three-overload test. Predict each line before you run it
- [ ] Explain why `f(makeConstW())` picks `const W&`, and why that makes returning `const T` by
      value a mistake
- [ ] Write a move constructor for `Tracer` **without** `std::move` on the member, and confirm
      with a nested tracer that the member is copied
- [ ] Answer: *why is a named `W&&` parameter an lvalue? What bug does that prevent?*
- [ ] Answer: *why does `std::move` on a copy-only type compile and silently copy?*

Next: [04 — Copy Semantics](04-copy-semantics.md), where we fix `BrokenString` properly and
meet the self-assignment trap.
