# 11.3 — Moving an owner, and the rule of five

Deep copy (§11.2) is correct but can be *expensive*: copying a million-element vector to return it
from a function duplicates a million elements, then destroys the original. Often the source is about
to die anyway (a temporary, a local being returned) — so instead of copying its resource, you can
**steal** it. That's move semantics.

## 1. What a move is

Your book's framing `[MEM §14.4]`:

> "Move Semantics … allows for performance improvements by transferring ownership of resources from
> one object to another, rather than copying data, significantly reducing the cost associated with
> transferring large objects like std::vector or std::string." `[MEM §14.4]`
>
> "Using std::move, ownership of an object is transferred rather than copied." `[MEM §14.4]`

A **move constructor** takes the source's internal pointer, nulls out the source so it won't free it,
and is done — O(1), no element copying:

```cpp
IntArray(IntArray&& o) noexcept            // && = rvalue reference: "o is safe to cannibalise"
    : p(o.p), n(o.n) {                     // steal the pointer and size
    o.p = nullptr; o.n = 0;                // leave o in a valid, destructible state (its ~ frees nothing)
}
```

cppreference states both the mechanism and the post-condition `[CPPREF-move-ctor]`:

> "Move constructors typically transfer the resources held by the argument … rather than make copies
> of them, and leave the argument in some valid but otherwise indeterminate state." `[CPPREF-move-ctor]`

"**valid but indeterminate**" is the contract for a moved-from object: you may still destroy it or
assign to it (those must work), but you may not assume anything about its value. For `IntArray` the
moved-from state is `p=nullptr, n=0` — a perfectly good empty array.

## 2. `std::move` is just a cast

`std::move(x)` does **not** move anything. It is a cast that produces an **rvalue reference** to `x`,
which makes overload resolution pick the move constructor/assignment instead of the copy ones. It's a
way of telling the compiler "I promise I'm done with `x`; you may steal from it." The actual stealing
is done by the move constructor you wrote. (So `std::move` on a `const` object silently *copies* —
there's nothing to permit stealing of a const.)

An **rvalue** is roughly a temporary or an about-to-die value (`f()`, `a + b`, `std::move(x)`); an
**lvalue** is a named object you'll use again. The compiler routes rvalues to move operations and
lvalues to copy operations automatically — `std::move` is how you *opt* a named lvalue into the move
path.

## 3. Move assignment, and the rule of five

The move assignment operator releases the target's current resource, then steals the source's:

```cpp
IntArray& operator=(IntArray&& o) noexcept {
    if (this != &o) {                      // guard against v = std::move(v)
        delete[] p;                        // release our current resource
        p = o.p; n = o.n;                  // steal
        o.p = nullptr; o.n = 0;            // null out the source
    }
    return *this;
}
```

Now the type has **five** special members: destructor, copy ctor, copy assign, move ctor, move assign.
That's the **rule of five**, and cppreference explains why it's five and not three `[CPPREF-rule]`:

> "Because the presence of a user-defined (include `= default` or `= delete` declared) destructor,
> copy-constructor, or copy-assignment operator prevents implicit definition of the move constructor
> and the move assignment operator, any class for which move semantics are desirable, has to declare
> all five special member functions." `[CPPREF-rule]`

In other words: the moment you declare a destructor (which an owner must), the compiler **stops
generating move operations** — so a rule-of-three type is silently *copy-only*, and every "move" of it
is really an expensive copy. To get moves back you must declare them. (The implicit move ctor is
suppressed by any user-declared copy ctor, copy assign, move assign, **or destructor** `[CPPREF-move-ctor]`.)

## 4. Mark moves `noexcept` — it's not optional for containers

Move operations should almost always be `noexcept`, and this is the hinge the whole exercise turns on:

> "To make the strong exception guarantee possible, user-defined move constructors should not throw
> exceptions. For example, std::vector relies on std::move_if_noexcept to choose between move and copy
> when the elements need to be relocated." `[CPPREF-move-ctor]`

If your move ctor is **not** `noexcept`, `std::vector` (and your Lesson-11 `Vector`) will **copy**
your objects instead of moving them when it grows — throwing away the entire performance benefit — to
preserve the strong guarantee (§11.4). A stealing move that only reassigns pointers *can't* throw, so
mark it `noexcept` and mean it. §11.4 and the exercise make this concrete with measurements.

## 5. Spot the book's error: `return std::move(local)`

The book's move example ends a function with `return std::move(temp);` `[MEM §14.4]`:

```cpp
std::vector<int> create_large_vector() {
    std::vector<int> temp(1000000, 42);
    return std::move(temp);   // <-- the book does this; it is a pessimization
}
```

This is a well-known **anti-pattern**. Returning a local by value already moves (and usually *elides*
the move entirely via NRVO — named return value optimization — constructing `temp` directly in the
caller's storage, zero moves). Writing `std::move` on the return defeats NRVO: `std::move(temp)` is an
rvalue reference, not a named object, so the compiler can no longer elide, and you force an extra move
`[CPPREF-copy-elision]`. The correct code is just:

```cpp
    return temp;              // NRVO elides the move; if not elided, it still moves. Never slower.
```

### (measured) NRVO vs the pessimization
A `Noisy` type that prints on copy/move, on your VM:
```
return temp;            (expect NO output = NRVO):        (nothing printed — move elided)
return std::move(temp); (expect 'move' = pessimized NRVO): move
```
`return temp;` elided the move entirely (no output); `return std::move(temp);` forced a move
`(measured; m11.cpp)`. Rule: **return local objects by name; never wrap a return in `std::move`.**
(The exception: returning a *member* or a moved-into *parameter*, where NRVO doesn't apply — there
`std::move` is correct.)

## Drills
1. Add the move ctor and move assign to `IntArray`. Then benchmark returning a 10⁶-element `IntArray`
   by value with and without a move ctor present (temporarily `= delete` the move to force copy).
   Measure the difference.
2. Reproduce `m11.cpp`'s NRVO demo. Add a function `IntArray f(IntArray a){ return a; }` — does
   `return a;` move or copy here, and why can't it be elided? (When is `std::move` on return correct?)
3. Remove the `noexcept` from `IntArray`'s move ctor. Put `IntArray`s in a `std::vector`, force a
   reallocation, and show (with copy/move counters) that the vector now **copies** them. Relate to
   §11.4 and `[CPPREF-move-ctor]`.
4. What is the state of a moved-from `std::string s2 = std::move(s1);`? Which operations on `s1` are
   still legal, and which are UB? Tie to "valid but indeterminate" `[CPPREF-move-ctor]`.

## My summary
