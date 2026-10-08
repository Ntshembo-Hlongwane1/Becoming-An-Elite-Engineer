# 11.1 — RAII and the rule of zero

## 1. The problem: manual release is unreliable under exceptions

You can already allocate and free (Lessons 7–8). The trouble is *remembering* to free on **every**
exit path — and an exception creates exit paths you didn't write. Your book names the three failure
modes precisely `[MEM §4.2]`:

> - "Memory Leaks: Occur when memory is allocated but not freed, leading to resource depletion.
> - Unsafe Resource Usage: Can occur if resources are improperly initialized or released, such as
>   accessing invalid pointers.
> - Exception Handling Issues: If an exception is thrown while a resource is allocated or used, that
>   resource may not be released, causing memory management and security issues." `[MEM §4.2]`

Concretely:

```cpp
void f() {
    int* p = new int[100];
    do_work(p);          // if this throws, the next line never runs ...
    delete[] p;          // ... and the 100 ints leak
}
```

A `try/catch` with `delete[]` in the handler *and* on the success path works but is verbose and easy
to get wrong (early `return`s, multiple resources, nested throws). C++ has a better mechanism.

## 2. The idea: tie the resource to an object's lifetime

**RAII — Resource Acquisition Is Initialization.** Your book's definition `[MEM §4.2]`:

> "RAII is a programming pattern in C++ that focuses on allocating resources during object
> initialization and releasing them when objects are destroyed. This pattern relies on the feature of
> calling constructors when objects are created and destructors automatically when objects go out of
> scope."
>
> "The basic idea in RAII is to associate resources with objects via constructors and destructors,
> where resources are allocated in the constructor and released in the destructor." `[MEM §4.2]`

The key language guarantee that makes it *exception-safe*: when the stack unwinds because of an
exception, C++ runs the destructor of **every** fully-constructed local on the way out. So a resource
held by a local is released whether the scope exits normally **or** by exception — automatically, once.
Your book: RAII "ensures safe release of resources even if exceptions occur." `[MEM §4.2]`

```cpp
struct IntArray {                      // a minimal RAII owner
    int* p;
    explicit IntArray(std::size_t n) : p(new int[n]) {}   // acquire in ctor
    ~IntArray() { delete[] p; }                            // release in dtor — runs on EVERY exit
};
void f() {
    IntArray a(100);
    do_work(a.p);        // if this throws, a's destructor still runs during unwinding -> no leak
}                        // normal exit: a's destructor runs here
```

This is why the lock in a mutex guard, the file in a file wrapper, and the memory in a smart pointer
are all "freed when the variable goes out of scope" — they are RAII owners. (`[MEM §4.2]` uses
`std::unique_ptr` as its example; Lesson 12 builds one.)

## 3. The owner is responsible for the whole lifecycle

The instant a type has a destructor that frees something, it has taken on **ownership** of that
resource, and ownership raises questions the compiler will answer *for* you — usually wrongly (§11.2):
- What happens when you **copy** an `IntArray`? (Default: copy the pointer → two owners → double
  `delete[]` → crash. §11.2.)
- What happens when you **move** one? (§11.3.)
- Is the resource released **exactly once** on every path? (The rule-of-five's job, §11.3.)

So "I wrote a destructor" is a commitment to write (or `=default`/`=delete`) the rest of the special
members. That is the rule of three/five (§§11.2–11.3), summarised by the C++ Core Guidelines:

> "If you define or =delete any copy, move, or destructor function, define or =delete them all."
> `[CORE-C.21]`

## 4. The rule of zero: the best special members are the ones you don't write

There's a way to owe *none* of that machinery: **don't manage raw resources yourself.** Build your
class out of members that are already RAII owners — `std::vector`, `std::string`, `std::unique_ptr` —
and the compiler-generated destructor, copy, and move are all correct automatically, because each
member knows how to destroy/copy/move itself. cppreference states it `[CPPREF-rule]`:

> "Classes that have custom destructors, copy/move constructors or copy/move assignment operators
> should deal exclusively with ownership … Other classes should not have custom destructors, copy/move
> constructors or copy/move assignment operators." `[CPPREF-rule]`

```cpp
struct Widget {                 // rule of zero: no dtor, no copy/move written
    std::string name;           // each member is already an RAII owner...
    std::vector<int> data;      // ...so the implicit special members are all correct
};                              // copyable, movable, leak-free — you wrote nothing
```

This is the real target in production code: **most** classes should be rule-of-zero. You hand-write
the rule of five only in the small number of types whose single job *is* to own a resource — an
allocator (Lesson 10), a smart pointer (Lesson 12), a container (Lesson 13, and this lesson's
`Vector<T>`). You write the hard version *once*, inside such a type, so that every type built from it
gets the rule of zero for free. Understanding the hard version is why this lesson exists even though
you'll mostly write rule-of-zero.

## Drills
1. Rewrite the leaking `f()` of §1 three ways: (a) `try/catch`, (b) the RAII `IntArray`, (c) rule of
   zero with `std::vector<int>`. Which is shortest and which is safest against a *second* resource
   being added later? Why?
2. Add a `std::ofstream` and a `std::lock_guard` to a function that also allocates. Count the exit
   paths (including exceptions). How many `delete`/`unlock`/`close` calls would the manual version
   need, and how many does RAII need?
3. For `struct Widget` in §4, write down the four special members the compiler generates and argue
   each is correct *because* of the member types. Now add a raw `int* p = new int;` member — which
   generated members become wrong, and what breaks (§11.2 preview)?
4. The book says the destructor runs "when objects go out of scope." Name the *other* trigger (stack
   unwinding) and give a two-line example where only that trigger prevents a leak.

## My summary
