# 10.4 — Plugging in: the `Allocator` interface and `std::pmr`

You have an arena and a pool. To make `std::vector`/`std::map`/`std::string` allocate through them,
you must speak the standard library's allocator protocol. There are two: the classic **`Allocator`**
(a template, compile-time) and C++17's **`std::pmr`** (runtime polymorphism). Know both; prefer pmr.

## 1. The classic `Allocator` (template, compile-time)

Every standard container has a second template parameter, the allocator, defaulting to
`std::allocator<T>`. A minimal conforming allocator needs only a `value_type` and `allocate`/`deallocate`;
your book's `EmbeddedAllocator` is the whole thing `[MEM §18.x]`:

```cpp
template <typename T>
struct EmbeddedAllocator {
    using value_type = T;
    T* allocate(std::size_t n)              { return static_cast<T*>(::operator new(n * sizeof(T))); }
    void deallocate(T* p, std::size_t) noexcept { ::operator delete(p); }
};
std::vector<int, EmbeddedAllocator<int>> vec;   // vector now allocates through it
```

The library fills in everything else via **`std::allocator_traits<A>`** — a layer that provides
defaults (construct/destroy, pointer types, `max_size`) for anything your allocator omits, and does
**rebind**: a `std::list<T, A>` doesn't allocate `T`s, it allocates list *nodes*, so it needs
`A::rebind<Node>` — `allocator_traits` derives that for you `[CPPREF-allocator]`. You rarely implement
traits; you implement the two functions and let traits adapt them.

**The problem with this design:** the allocator is a **template parameter**, so it's part of the
container's *type*. `std::vector<int>` and `std::vector<int, MyAlloc<int>>` are different, incompatible
types — you can't pass one to a function expecting the other, can't store both in one container, and
the choice is frozen at compile time. The allocator "infects" every type it touches. That friction is
why almost nobody used custom allocators before C++17.

## 2. `std::pmr`: move the choice to runtime

C++17's `<memory_resource>` splits the allocator into two pieces:

- **`std::pmr::memory_resource`** — an *abstract base class* that is the actual allocator. "The class
  `std::pmr::memory_resource` is an abstract interface to an unbounded set of classes encapsulating
  memory resources." `[CPPREF-memres]` You derive from it and implement three **private virtual**
  functions; the public non-virtual `allocate/deallocate/is_equal` forward to them `[CPPREF-memres]`:

  ```cpp
  class ArenaResource : public std::pmr::memory_resource {
      void* do_allocate(std::size_t bytes, std::size_t alignment) override;          // §signatures below
      void  do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override;
      bool  do_is_equal(const std::pmr::memory_resource& other) const noexcept override;
  };
  ```
  - `do_allocate` "Allocates storage with a size of at least `bytes` bytes, aligned to the specified
    `alignment`"; "`alignment` shall be a power of two"; and "Throws an exception if storage of the
    requested size and alignment cannot be obtained." `[CPPREF-do_allocate]` (For an arena backing it,
    this is §10.2's bump; `do_deallocate` is a no-op, §10.2 §2.)
  - `do_is_equal` returns whether memory from one resource can be freed through the other — for a
    stateful arena that means "`&other == this`" (only the same arena can free its blocks).

- **`std::pmr::polymorphic_allocator<T>`** — a thin `Allocator` that holds a `memory_resource*` and
  forwards to it. It "is an Allocator which exhibits different allocation behavior depending upon the
  `std::pmr::memory_resource` from which it is constructed." `[CPPREF-polyalloc]` Crucially the
  *allocator type never changes* — only the pointer inside it does — so:

  > "different container instances with `polymorphic_allocator` as their static allocator type are
  > interoperable, but can behave as if they had different allocator types." `[CPPREF-polyalloc]`

`std::pmr::vector<int>` is just `std::vector<int, std::pmr::polymorphic_allocator<int>>`. You construct
it with a pointer to any `memory_resource`, and it allocates there — chosen **at runtime**:

```cpp
ArenaResource res{/*buffer*/};
std::pmr::vector<int> v{&res};      // this vector draws from the arena
std::pmr::vector<int> w;            // this one uses the default heap
// v and w are the SAME TYPE — no template infection
```

### (measured) a pmr::vector living in a buffer
Using the standard `monotonic_buffer_resource` over a 1 KB stack array:
```
pmr::vector data() = 0x7ffe...5bcc   stack buffer = [0x7ffe...59d0, 0x7ffe...5dd0)   inside_buffer=yes
```
The vector's element storage is **inside the stack buffer** — no heap allocation at all `(measured; m10.cpp)`.
That's the whole point: a normal-looking `std::pmr::vector` backed by storage *you* chose.

## 3. The ready-made resources

You usually don't write a `memory_resource` from scratch — the standard ships several, which are the
arena and pool of §§10.2–10.3 in library form:
- `std::pmr::monotonic_buffer_resource` — the arena (§10.2): fast, deallocate is a no-op, releases on
  destruction/`release()` `[CPPREF-monotonic]`.
- `std::pmr::unsynchronized_pool_resource` / `synchronized_pool_resource` — pools (§10.3) that manage
  several size-classes (the `synchronized` one is thread-safe; the `unsynchronized` one isn't, and is
  faster). They pull their buffers from an **upstream** resource.
- `std::pmr::new_delete_resource()` — the default: forwards to global `operator new`/`delete` (Lesson 8).
- `std::pmr::null_memory_resource()` — always throws; useful as an upstream to *forbid* heap fallback,
  so a `monotonic_buffer_resource{buf, size, null_memory_resource()}` is strictly bounded to `buf`.

You implement a `memory_resource` yourself (as in the exercise) when your allocation strategy isn't one
of these — or, as here, to *learn* the interface by wrapping your own arena.

## 4. One sharp edge: pmr allocators don't propagate

`polymorphic_allocator` "does not propagate on container copy assignment, move assignment, or swap"
`[CPPREF-polyalloc]`. Consequence, quoted: "move assignment of a `polymorphic_allocator`-using
container can throw, and swapping two … containers whose allocators do not compare equal results in
undefined behavior." `[CPPREF-polyalloc]` Practical rule: don't `swap`/move-assign two pmr containers
built on *different* resources; keep a container with the resource it was born with. (It's the price of
runtime polymorphism — the allocator can't be silently carried across objects the way a value-type one
could.)

## Drills
1. Write the three `do_*` overrides for an `ArenaResource` wrapping §10.2's `Arena`: `do_allocate` →
   bump, `do_deallocate` → no-op, `do_is_equal` → `this == &other`. Then drive a `std::pmr::vector<int>`
   with it and confirm (print addresses) the data lands in the arena (the exercise).
2. Why does `std::list<int, A>` need `A` to rebind, while `std::vector<int, A>` essentially doesn't?
   (Hint: what does each container actually allocate? §1.)
3. Show the "template infection" concretely: write a function taking `std::vector<int>&` and try to
   pass a `std::vector<int, EmbeddedAllocator<int>>`. What's the error, and how does `std::pmr::vector`
   avoid it? (§1–§2)
4. Build a `monotonic_buffer_resource{buf, size, std::pmr::null_memory_resource()}` and push past
   `size` elements into a `pmr::vector` on it. What happens, and why is that sometimes exactly what you
   want (embedded / hard bounds)? (§3)

## My summary
