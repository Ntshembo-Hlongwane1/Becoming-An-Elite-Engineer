# 11.2 — Copying an owner, and the rule of three

## 1. What the compiler gives you by default, and why it's wrong for owners

If you don't write them, the compiler generates a **copy constructor** and **copy assignment
operator** that copy each member *bitwise/memberwise*. For a rule-of-zero type (§11.1 §4) that's
exactly right — each member copies itself. For a type that owns a **raw** resource it is a disaster:

```cpp
struct IntArray {
    int* p; std::size_t n;
    IntArray(std::size_t n) : p(new int[n]), n(n) {}
    ~IntArray() { delete[] p; }
    // no copy ctor written -> compiler generates: copies p (the POINTER) and n
};

IntArray a(10);
IntArray b = a;      // default copy: b.p == a.p  (SAME allocation!)
// ... end of scope ...
// ~b runs: delete[] b.p;   then
// ~a runs: delete[] a.p;   -> DOUBLE FREE of the same block (Lesson 7.5 primitive)
```

The default copy did a **shallow copy**: it duplicated the pointer, not the thing it points to, so now
*two* `IntArray`s believe they own *one* allocation. When both destructors run you get a double-free;
and writing through `a` is visible through `b` (unintended aliasing). This is the single most common
beginner memory bug, and it's why owning types must define copy.

## 2. Deep copy: duplicate the resource, not the handle

A correct copy constructor allocates its **own** resource and copies the *contents*:

```cpp
IntArray(const IntArray& o) : p(new int[o.n]), n(o.n) {   // own, separate allocation
    std::copy(o.p, o.p + n, p);                            // copy the VALUES
}
```

Now `b = a` gives `b` an independent block; each destructor frees its own; no double-free, no aliasing.
This is a **deep copy**, and "copy = independent duplicate" is the **value semantics** that makes C++
objects behave like `int`s rather than like shared pointers.

## 3. Copy assignment: also handle self-assignment and the existing resource

Copy *assignment* is harder than the constructor because the target **already owns** a resource that
must be released, and the source might *be* the target:

```cpp
IntArray& operator=(const IntArray& o) {
    if (this == &o) return *this;          // 1. self-assignment guard (a = a must be a no-op)
    int* q = new int[o.n];                 // 2. allocate the NEW resource FIRST (may throw)
    std::copy(o.p, o.p + o.n, q);          //    ...before touching our own state
    delete[] p;                            // 3. release the OLD resource
    p = q; n = o.n;                        // 4. adopt the new one
    return *this;
}
```

Order matters for **exception safety** (§11.4): allocate-and-copy the new buffer *before* deleting the
old one, so that if `new`/copy throws, `*this` is unchanged (you haven't freed anything yet) — the
strong guarantee. The naïve order (`delete[] p; p = new int[o.n];`) leaves `*this` holding a freed
pointer if the `new` throws. §11.4's **copy-and-swap** idiom gets this correctness for free and is what
the exercise uses.

## 4. The rule of three

The three operations are a package. cppreference `[CPPREF-rule]`:

> "If a class requires a user-defined destructor, a user-defined copy constructor, or a user-defined
> copy assignment operator, it almost certainly requires all three." `[CPPREF-rule]`

The logic (derived): needing a custom **destructor** means the class owns a raw resource; owning a raw
resource means the default (shallow) **copy** is wrong; so you need custom copy ctor *and* copy assign
too. Conversely, if you found you needed a custom copy, you're managing something that also needs
freeing → you need a destructor. They stand or fall together.

If a type should **not** be copyable (a unique owner — a mutex, a file, `unique_ptr`), you say so
explicitly by `= delete`-ing the copy operations rather than leaving them to do the wrong thing:

```cpp
IntArray(const IntArray&) = delete;
IntArray& operator=(const IntArray&) = delete;   // now copying is a compile error, not a double-free
```

That still "defines all three" (destructor + two deleted copies) and is the honest choice when a deep
copy makes no sense. The next file adds the fourth and fifth members — **move** — which is how you get
cheap transfer back for such unique owners.

## Drills
1. Take the shallow-copy `IntArray` of §1, run it under ASan with `IntArray b = a;`. What exact error
   does ASan report at scope exit, and which Lesson-7.5 primitive is it?
2. Write the deep copy ctor and the §3 copy assignment. Then test `a = a;` (self-assign) and confirm
   it neither frees-then-reads nor leaks. Why does the self-assignment guard matter *specifically*
   for the naïve `delete[] p; p = new...` order?
3. Why is allocating the new buffer *before* `delete[] p` the exception-safe order? Construct the
   failure (make `new` throw via a huge size) and show the naïve order leaves a dangling `p`.
4. You have `struct Unique { int* p; ~Unique(){ delete p; } };`. It should be non-copyable. Write the
   two `= delete` lines and show that `Unique b = a;` now fails to *compile* (the good kind of failure).

## My summary
