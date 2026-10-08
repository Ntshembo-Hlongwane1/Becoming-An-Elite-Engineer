# 8.2 — `operator new` and `operator delete`: the allocation functions

Step 1 of a `new` expression (§8.1) calls an **allocation function**. These are ordinary functions —
you can call them directly, overload them, and replace them. This file is the reference for the whole
family, because the exercise replaces most of it.

## 1. What they are

> "Attempts to allocate requested number of bytes… These allocation functions are called by `new`
> expressions to allocate memory in which [the] new object would then be initialized." `[CPPREF-opnew]`

`operator new(std::size_t n)` takes a byte count and returns a `void*` to raw, uninitialised,
suitably-aligned storage — i.e. it is `malloc` with a C++ signature and C++ failure behaviour. The
default library version literally obtains the memory the Lesson-7 way (it calls `malloc` or
equivalent). Your book uses it directly inside custom allocators — e.g. a memory pool that falls back
to `::operator new(size)` when it has no recycled block `[MEM §10.x, MemoryPool::allocate]`, and a
storage system whose `allocate`/`deallocate` are thin wrappers over `::operator new`/`::operator delete`
`[MEM §10.x, StorageSystem]`. The leading `::` means "the global one" (as opposed to a class's own, §5).

## 2. The replaceable family

There is not one allocation function but a set. cppreference lists the **replaceable global** forms
`[CPPREF-opnew]`:

| Form | Signature | Notes |
|---|---|---|
| throwing, single | `void* operator new(std::size_t)` | the common one; throws `bad_alloc` on failure |
| throwing, array | `void* operator new[](std::size_t)` | used by `new T[]` |
| nothrow, single | `void* operator new(std::size_t, const std::nothrow_t&)` | returns `nullptr` on failure; `noexcept` |
| nothrow, array | `void* operator new[](std::size_t, const std::nothrow_t&)` | |
| aligned, single | `void* operator new(std::size_t, std::align_val_t)` | **since C++17**, for over-aligned types |
| aligned, array | `void* operator new[](std::size_t, std::align_val_t)` | since C++17 |

and the matching `operator delete` forms `[CPPREF-opdelete]`, including the **sized** deletes
`operator delete(void*, std::size_t)` (the compiler passes the object size back on delete so the
allocator needn't look it up — the `n1.cpp` (measured) in §8.1 used this). All deallocation functions
are `noexcept`.

Two rules fall out of this table and matter for the exercise:
- **Throwing vs nothrow.** "Throws an exception of a type that would match a handler of type
  `std::bad_alloc` on failure"; the nothrow versions are "the same… but return a null pointer if the
  allocation fails." `[CPPREF-opnew]` So `new T` throws; `new (std::nothrow) T` returns `nullptr`.
  Choose nothrow only when you genuinely handle `nullptr` at the call site.
- **They come as a matched set.** If you replace the single-object throwing `operator new`, you should
  replace its partner `operator delete` (and, because `new[]` exists, the array pair, and the sized
  deletes the compiler may emit). Replacing half the set is the classic way to get a crash where a
  `new` from *your* allocator is freed by the *default* `delete`.

## 3. Alignment guarantee

Like `malloc` (Lesson 7.2 §3), `operator new` must return storage aligned for any ordinary object:

> "the storage is aligned to `__STDCPP_DEFAULT_NEW_ALIGNMENT__`" `[CPPREF-opnew]`

### (measured) on your VM
```
__STDCPP_DEFAULT_NEW_ALIGNMENT__ = 16
```
Source: `n1.cpp`. So the default `operator new` hands back 16-aligned storage — the same 16 you
measured from `malloc`. A type whose `alignof` **exceeds** 16 (e.g. `alignas(64)` from the cache
lesson, Lesson 6.3) is *over-aligned*; for it the compiler calls the **aligned** `operator new(size,
align_val_t)` form instead (that's why C++17 added it). Your exercise handles the default-aligned
forms and leaves over-aligned ones to the default library versions — a drill extends it.

## 4. When allocation fails: `bad_alloc` and the new-handler

The throwing `operator new` doesn't just `return nullptr` and hope — on failure it throws
`std::bad_alloc`. Your book shows the pattern `[MEM §7.x]`:

```cpp
try {
    // ... allocate ...
    throw std::bad_alloc();            // what operator new does internally on failure
} catch (const std::bad_alloc& e) { /* handle */ }
```

Before throwing, the default `operator new` first calls the installed **new-handler** (a function you
can set with `std::set_new_handler`) in a loop, giving it a chance to free memory and let the
allocation retry; only when there's no handler (or it fails to help) does it throw `bad_alloc`
**(derived from the standard's operator-new behaviour; see `[CPPREF-opnew]`)**. You rarely install one,
but knowing it exists explains why `operator new` is specified as a *loop*, not a single `malloc`.

## 5. Class-specific `operator new`

A class may provide its own `operator new`/`operator delete` as static members; then `new MyClass`
uses *those* instead of the global ones — a per-type allocator hook, without touching the rest of the
program. This is how an object type opts into a pool (Lesson 10) while everything else stays on the
global allocator. The global replacement you do in the exercise is the whole-program version of the
same idea; class-specific is the scalpel, global is the blanket. (We implement the global one because
the exercise's goal is to count *every* allocation.)

## Drills
1. Write a call to each of: throwing `new`, `new (std::nothrow)`, `new[]`, and (for an `alignas(64)`
   type) the aligned form — then predict which `operator new` overload each one calls (§2).
2. `new (std::nothrow) T` — if `T`'s *constructor* throws (not the allocation), do you get `nullptr`
   or an exception? (Hint: nothrow is about the *allocation function*, not step 2. §8.1 §5.)
3. Install a `std::set_new_handler` that prints and then calls `std::abort`. Force an allocation
   failure (ask for a huge size) and confirm your handler runs before any `bad_alloc`. Relate to §4.
4. Give a class a static `operator new`/`operator delete` that log, and confirm `new MyClass` uses
   them while `new int` still uses the global ones (§5).

## My summary
