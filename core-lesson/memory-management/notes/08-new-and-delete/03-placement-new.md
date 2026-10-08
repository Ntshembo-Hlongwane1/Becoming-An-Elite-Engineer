# 8.3 — Placement new: constructing in storage you already own

Step 2 of a `new` expression (§8.1) — running a constructor in raw storage — is itself a callable
operation: **placement new**. It is `new` with the allocation removed. Once you can call it directly,
you can separate *where memory comes from* (your buffer, an arena, a pool) from *when an object is
built there*. Every container, allocator, `optional`, and `variant` in the standard library is built
on it, and so is the exercise and all of Phase B/C after it.

## 1. The syntax and what it does

Include `<new>`. The non-allocating placement form is:

> "`new (ptr) Type` … the standard allocation function `void* operator new(std::size_t, void*)`…
> simply returns its second argument unchanged." `[CPPREF-new]`

So `new (ptr) T(args)` performs **no allocation** — the "allocation function" just hands back `ptr` —
and then constructs a `T` at `ptr`. cppreference's own example `[CPPREF-new]`:

```cpp
#include <new>
alignas(T) unsigned char buf[sizeof(T)];   // raw, correctly-aligned storage you already own
T* tptr = new (buf) T;                       // construct a T directly into buf — no heap allocation
// ... use *tptr ...
tptr->~T();                                   // you MUST destroy it by hand (see §3)
```

Read that against §8.1: an ordinary `new T` is exactly `new (operator new(sizeof(T))) T`. Placement
new is the second half, exposed, with *you* supplying the storage pointer.

## 2. Why it exists — the storage/lifetime split

`malloc`/`operator new` give you **storage**; a constructor gives you an **object** (Lesson 2). Keeping
them separate is what lets a container like `std::vector` do the thing that makes it fast:

- `vector` allocates capacity for, say, 16 `T`s **once** (one `operator new`/allocator call for raw
  bytes), but there are **zero** `T` objects yet.
- Each `push_back` runs **placement new** to construct one `T` in the next raw slot — no per-element
  allocation.
- Each `pop_back`/erase runs an explicit **destructor call** on that slot; the capacity stays.
- Only when it outgrows capacity does it allocate again and move the objects over.

Without the split you'd have to default-construct all 16 up front (wrong, and often impossible — `T`
may have no default constructor) or allocate per element (slow). Placement new over pre-allocated
storage is the mechanism behind `vector`'s amortised O(1) growth (Lesson 13 builds this), behind
arenas and pools (Lesson 10), and behind `std::optional`/`std::variant` (an object that may or may
not exist inside fixed storage). It is also why Lesson 7's exercise returned *raw bytes*: you place
objects into them yourself.

## 3. You must destroy by hand — the rule that bites

Because placement new did **not** allocate, you must **not** `delete` the pointer — there is no
allocation for `delete` to free, and `delete` would also call `operator delete` on storage you didn't
get from `operator new`. Instead you call the destructor explicitly and then dispose of the storage
however you obtained it `[CPPREF-new]`:

```cpp
tptr->~T();          // end the object's lifetime (runs the destructor, nothing is freed)
// then free `buf`'s storage by its OWN rule: nothing if it's a stack array; operator delete / free /
// your allocator's deallocate if that's where it came from.
```

Two failure modes to internalise:
- **`delete tptr;` after placement new into a stack buffer** → UB: `operator delete` is handed a stack
  address it never allocated.
- **forgetting `tptr->~T();`** → the destructor never runs: leaks whatever `T` owns (memory, file
  handles), even though the raw storage itself may be reclaimed. This is a *resource* leak that ASan's
  memory-leak check won't necessarily catch, because the bytes were freed — only `T`'s owned resources
  leaked. (Lesson 11's RAII exists to make this impossible.)

For arrays you placement-new a loop of constructors and must destroy them in a loop (in reverse),
because there is no array cookie (§8.1 §4) on a manual placement — you track the count yourself.

## 4. The C++17/20 vocabulary: `construct_at`, `destroy_at`, `launder`

Modern C++ gives named, `constexpr`-friendly spellings of these two half-operations so you don't
hand-write placement-new/`->~T()` and can use them in `constexpr` and allocator code `[CPPREF-construct_at]`:

- `std::construct_at(p, args...)` — "Creates a `T` object initialized with `args...` at the address
  `p`" — i.e. placement new, as a function (since C++20).
- `std::destroy_at(p)` — calls `p->~T()` (and for arrays, destroys each element). `std::destroy`,
  `std::uninitialized_copy`, etc. are the range versions used by containers.
- `std::launder` (C++17) — a *barrier* you need in rare cases after placement-new'ing a **new** object
  into storage that held a different object, to stop the compiler from reusing a stale pointer's
  assumptions. You will almost never need it directly; recognise it as "tell the optimiser the object
  at this address was replaced." (Relevant when you reuse one buffer for successive objects, as a
  pool might.)

Prefer `construct_at`/`destroy_at` in new code; the exercise and this lesson use raw placement new +
explicit `~T()` once, so the mechanism is visible, then you switch to the named forms.

## 5. Placement new over Lesson-7 storage (the through-line)
Concretely, to put a `std::string` into a chunk from your Lesson-7 allocator:
```cpp
void* raw = my_allocator.allocate(sizeof(std::string));     // Lesson 7: raw, 16-aligned bytes
auto* s   = new (raw) std::string("hello");                  // Lesson 8: construct in place
// ... use *s ...
s->~string();                                                // destroy the object
my_allocator.deallocate(raw);                                // Lesson 7: return the storage
```
That four-line pattern — allocate raw / placement-new / explicit destroy / deallocate raw — is the
skeleton of every allocator-aware container and smart pointer you'll write in Lessons 10–13. `new`/`delete`
is just this pattern with the global `operator new`/`operator delete` filled in for the first and last
lines, which is exactly what the exercise instruments.

## Drills
1. Build a `std::aligned_storage`-style buffer `alignas(T) unsigned char buf[sizeof(T)];`, placement-new
   a `std::string` into it, use it, destroy it. Run under ASan — confirm **no** leak and **no**
   `delete`. Then *deliberately* omit the `~string()` and see what leaks (and what ASan says).
2. Rewrite drill 1 using `std::construct_at`/`std::destroy_at`. Confirm identical behaviour (§4).
3. Why can a `vector<T>` hold a `T` that has *no default constructor*, while `new T[n]` cannot?
   (Hint: placement-new per element with real args vs default-construct-all. §2.)
4. You placement-new two different objects, one after another, into the *same* `buf`. What must you do
   between them, and when might `std::launder` enter the picture? (§3, §4.)

## My summary
