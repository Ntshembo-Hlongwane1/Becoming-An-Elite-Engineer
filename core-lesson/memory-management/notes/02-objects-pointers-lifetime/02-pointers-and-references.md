# 2.2 — Pointers and References

## 1. A pointer is a value that names a byte

`[MEM §3.1]`: "A pointer is a variable that holds the memory address of another variable."

```cpp
int x = 10;
int* p = &x;      // p holds the address of x        (&  = "address of")
*p = 20;          // *p is the object at that address (*  = "dereference"); x is now 20
```

From Lesson 1 §… a pointer's *value* is just an unsigned integer (an address); the *type* `int*`
tells the compiler how many bytes an object there occupies and how to interpret them, and how
pointer arithmetic steps (§2.3). On your VM every pointer is 8 bytes **(measured, Lesson 1)**,
regardless of what it points to.

Three facts people get wrong:
- `int* p` vs `int *p` is the same declaration; the `*` binds to the declarator, not the type. In
  `int* a, b;` only `a` is a pointer — `b` is an `int`. (Declare one per line to avoid this.)
- A pointer has its own address: `&p` is an `int**` (§4).
- `sizeof(p)` is the pointer's size (8), not the pointee's. `sizeof(*p)` is the pointee's.

## 2. Null, invalid, and dangling

- A **null pointer** (`nullptr`) points to no object. Dereferencing it is UB (typically a crash via
  the unmapped zero page, Lesson 4). Always prefer `nullptr` over `0`/`NULL` — it has pointer type,
  so it can't be mistaken for an integer in overloads.
- An **invalid** pointer holds a bit pattern that isn't the address of a live object — uninitialised,
  or past-the-end-and-then-some. Even *forming* some invalid pointers is UB (§2.3); dereferencing
  one is always UB.
- A **dangling** pointer once pointed to a live object whose lifetime has since ended (freed, or
  went out of scope). It still holds the old address. Dereferencing it is use-after-free /
  use-after-scope (Lesson 2.5). `[MEM §1.3.3]` lists dangling pointers among the core hazards.

The hardware rarely stops you: a dangling read often "works" because the bytes are still there —
until the allocator reuses them. That's exactly why you build and test under ASan (Lesson 2.5).

## 3. `void*` — an address with no type

`void*` holds any object address but has no pointee type, so you cannot dereference it or do
arithmetic on it. It's the currency of raw memory APIs: `malloc` returns `void*`, `memcpy` takes
`void*`. To use the bytes you `static_cast` it to a concrete pointer (and then the aliasing rules of
§2.4 apply). `malloc`/`operator new` hand back `void*` precisely because they deal in untyped
storage (Lessons 7–8).

## 4. Pointer to pointer `[MEM §2.3.2]`

Because a pointer is itself an object with an address, you can point at it:

```cpp
int x = 5;
int*  p = &x;     // p -> x
int** pp = &p;    // pp -> p -> x
**pp = 9;         // writes x through two hops
```

Uses: an out-parameter that must set the caller's pointer (the classic C idiom, and
`posix_memalign(&ptr, …)` from the alignment lesson), and arrays of pointers (`char* argv[]`, which
is `char**`). `[MEM §2.3.2]` calls these "double pointers". You rarely need more than two levels; if
you do, a struct or a reference is usually clearer.

## 5. References — a name, not an object

A reference (`int& r = x;`) is an **alias**: another name for an existing object. Unlike a pointer it
cannot be null, cannot be rebound to another object, and needs no `*` to use. Under the hood a
compiler usually implements a reference as a pointer, but in the language it is not an object — you
can't take `sizeof` a reference meaningfully or make an array of references.

Rule of thumb for this course:
- **reference** when the thing must exist and you won't rebind (function parameters you don't copy,
  range-for);
- **pointer** when it can be null, can be reseated, or is doing raw-memory work;
- **`std::span`/`std::string_view`** (alignment lesson Part 4 §7) when you mean "a view of N bytes/
  elements I don't own" — it carries the length, which a bare pointer doesn't.

## 6. `const` and where it binds

```cpp
const int* p;        // pointer to const int   : *p is read-only, p can move
int* const p = &x;   // const pointer to int   : *p writable, p fixed
const int* const p;  // both
```

Read right-to-left: "`p` is a `const` pointer to `int`". `const` on the pointee (`const T*`) is the
one you'll use constantly for read-only views into buffers (`const std::byte*`).

## Drills
1. For `int a=1,b=2; int* p=&a;`, write expressions for: the value of `a` via `p`; the address of
   `p`; rebinding `p` to `b`; a `const` view of `a` that forbids writing through it.
2. Why does `void* v; int x = *v;` not compile? What's the minimum you must write to read an int from
   `v` (assuming it really points at an int)? (And §2.4 says even then, mind aliasing.)
3. Write a function `bool alloc_int(int** out)` that sets `*out` to a `new int(42)` and returns true;
   call it and free the result. Why the `int**`?
4. Predict `sizeof` for `int*`, `char*`, `void*`, `int**`, and a function pointer. Measure.

## My summary
