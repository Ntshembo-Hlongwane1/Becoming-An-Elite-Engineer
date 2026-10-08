# 10.2 — The arena (bump / monotonic) allocator

The arena is the simplest useful allocator and the one with the biggest speed win on its workload. It
is worth understanding exactly because the standard library ships one — `std::pmr::monotonic_buffer_resource`
(§10.4) — and because it makes the storage-vs-lifetime split from Lesson 8 unavoidable.

## 1. The mechanism: one pointer

An arena owns a fixed buffer `[base, base+cap)` and a single **offset** (the "bump pointer") into it.
To allocate `n` bytes with alignment `a`:
1. round the current offset **up** to a multiple of `a` (Lesson 9 — the returned pointer must be
   aligned);
2. if `aligned_offset + n > cap`, fail (return `nullptr` or fall back to an upstream allocator);
3. otherwise set `offset = aligned_offset + n` and return `base + aligned_offset`.

That's the entire allocate path — a rounding, a compare, an add. No search, no header, no free list.
In C++ (the core of the exercise):

```cpp
void* Arena::allocate(std::size_t n, std::size_t align) {
    std::size_t p = (offset_ + (align - 1)) & ~(align - 1);   // Lesson 9: round up to `align`
    if (p + n > cap_) return nullptr;                          // out of buffer
    offset_ = p + n;                                           // bump
    return base_ + p;
}
```

Because allocation is just a pointer bump, it is **O(1)** with a tiny constant and no cache misses —
the source of the ~50–75× over `malloc` you measured in §10.1.

## 2. The catch: you cannot free one object

There is no per-object `deallocate`. Once you've bumped past an object, there's no bookkeeping that
could let you reclaim just that object's bytes without leaving a hole — and holes are exactly the
fragmentation an arena exists to avoid. So the arena offers only:

- **`reset()`** — set `offset_ = 0`, instantly "freeing" **everything** at once (the next allocation
  reuses the buffer from the start).

This is why the arena is also called **monotonic**: within one cycle the offset only ever grows, until
you reset it. The standard library's version makes the same trade explicit:

> "`std::pmr::monotonic_buffer_resource` … releases the allocated memory only when the resource is
> destroyed." `[CPPREF-monotonic]`
> "It is intended for very fast memory allocations in situations where memory is used to build up a
> few objects and then is released all at once." `[CPPREF-monotonic]`
> its `do_deallocate` is a **"no-op"** `[CPPREF-monotonic]`.

A `deallocate(p)` on an arena legitimately does nothing. That is not a bug — it's the design.

## 3. `reset()` frees storage, not objects — the Lesson-8 trap

`reset()` reclaims the *bytes*. It does **not** run destructors — the arena only ever saw raw storage
(Lesson 8.1). So if you placement-new'd objects that own resources (a `std::string`, a file handle)
into the arena and then `reset()`, you have **leaked those resources** even though the arena bytes are
reusable. Correct use:

```cpp
auto* s = new (arena.allocate(sizeof(std::string), alignof(std::string))) std::string("hi");
// ... use *s ...
s->~string();          // Lesson 8.3: destroy objects BEFORE the storage is reset/reused
arena.reset();         // now safe
```

For arenas you typically either store only **trivially-destructible** objects (PODs, ints, nodes with
no owning members — then `reset()` alone is correct and glorious), or keep a list of the non-trivial
objects to destroy before reset. The standard containers handle this for you (they destroy elements in
their destructor); raw arena use does not. This is the single most common custom-allocator bug and
§10.5 treats it as a security issue too.

## 4. Owning the buffer

Where does `[base, cap)` come from? Three common choices, in rising order of flexibility:
- a **static/stack array** (`alignas(16) std::byte buf[N];`) — zero heap, perfect for embedded or a
  known bound; the arena doesn't own it, the array's own storage duration does.
- a single **`operator new`/`malloc`** block the arena owns and frees in its destructor (the exercise).
- an **upstream** allocator it pulls *more* buffers from when the first is exhausted — exactly what
  `monotonic_buffer_resource` does:

  > "`monotonic_buffer_resource` can be constructed with an initial buffer. If there is no initial
  > buffer, or if the buffer is exhausted, additional buffers are obtained from an upstream memory
  > resource supplied at construction." `[CPPREF-monotonic]`

The exercise's `Arena` owns one `operator new[]`'d buffer and frees it once; a drill adds upstream
fallback.

## 5. When to use it
- per-**frame** scratch in a game/renderer: allocate freely during the frame, `reset()` at the end.
- per-**request** scratch in a server: one arena per request, destroyed with the request.
- building an immutable structure once (parse tree, serialized blob) then walking it, then dropping it.
- **not** for objects with independent, overlapping lifetimes — that's what the pool (§10.3) or
  `malloc` is for.

## Drills
1. Why must the arena round the offset up *before* checking `p + n > cap`, not after bumping? Construct
   an `n`, `align`, `offset` where doing it in the wrong order returns a misaligned pointer (Lesson 9).
2. Implement `reset()` and show (print addresses) that the first allocation after a reset reuses the
   very first address. Then show the resource leak of §3 by resetting over a `std::pmr::string` without
   destroying it (watch ASan / a leak counter).
3. `monotonic_buffer_resource` falls back to an upstream resource when its buffer is full (§4). Sketch
   how you'd add that to the exercise's `Arena` without changing its `allocate` signature.
4. An arena gives out 50 objects, you `reset()`, then give out 50 more. How many destructor calls has
   the arena made? (Answer and the consequence — §3.)

## My summary
