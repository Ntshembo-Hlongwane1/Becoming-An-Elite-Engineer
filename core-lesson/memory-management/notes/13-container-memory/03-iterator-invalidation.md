# 13.3 — Iterator (and pointer, and reference) invalidation

## 1. What "invalidation" means

An iterator, pointer, or reference into a container names a *location*. When an operation moves the
elements to a new location — or removes the element they named — those old handles still hold the
**old** address, which no longer contains the element (it may be freed memory). Using one is undefined
behaviour: a **use-after-free** or a read of a stale slot. An operation that does this is said to
**invalidate** the handles.

For a `vector`, the cause is almost always **reallocation** (§13.1 §3): growing past capacity moves
everything to a new buffer and frees the old one.

## 2. The rules (vector)

cppreference gives the table `[CPPREF-vector]`:

| Operation | Invalidates |
|---|---|
| read-only access (`operator[]`, `at`, `begin`, iteration, …) | never |
| `reserve`, `shrink_to_fit` | **all** iterators/pointers/references if capacity changed; else none |
| `push_back`, `emplace_back` | **all** if it reallocated; else only `end()` |
| `insert`, `emplace` | **all** if it reallocated; else those **at or after** the insertion point |
| `resize` | **all** if it reallocated; else `end()` and any erased elements |
| `erase` | those at or after the erased element |
| `clear` | `end()`-ish; elements destroyed |

The `push_back` page states it exactly `[CPPREF-push_back]`:

> "If after the operation the new `size()` is greater than old `capacity()` a reallocation takes place,
> in which case all iterators (including the `end()` iterator) and all references to the elements are
> invalidated. Otherwise only the `end()` iterator is invalidated." `[CPPREF-push_back]`

The through-line from §13.1: **a reallocation invalidates everything; no reallocation invalidates
little.** So `reserve`-ing up front (§13.1 §4) isn't just a speed trick — it makes invalidation
*predictable* (no surprise reallocation mid-loop).

## 3. (measured) a reallocation leaves your pointer dangling
```
after growth past capacity: &w[0] changed = yes (old=0x…6030 new=0x…6050) -> old pointers dangling
```
A pointer taken to `w[0]` *before* a `push_back` that exceeded capacity points at the **old** buffer
after the reallocation; the element now lives at a new address `(measured; m13.cpp)`. Dereferencing or
writing through the old pointer is a use-after-free of the freed old buffer.

## 4. The classic bug (and why it's so common)

```cpp
std::vector<int> v = {1, 2, 3};
int& first = v[0];        // or: auto it = v.begin();  or: int* p = v.data();
for (int i = 0; i < 1000; ++i)
    v.push_back(i);       // somewhere in here, v reallocates...
std::cout << first;       // ...and `first` now dangles -> use-after-free (UB)
```

Variants that bite constantly:
- **holding an iterator/pointer/reference across a `push_back`/`insert`/`resize`** (above);
- **`v.push_back(v[0]);`** — if this reallocates, the argument `v[0]` is evaluated against the *old*
  buffer that gets freed mid-call (libstdc++ handles this specific case, but the pattern is a trap);
- **erasing while iterating** with a stale iterator (`erase` returns the next valid iterator for a
  reason);
- **a reference member caching `&container[i]`** that outlives a growth.

This is one of the most frequent memory-safety bugs in real C++, precisely because the code *looks*
fine — the invalidation is invisible at the call site. §13.5 treats it as the security issue it is.

## 5. Which containers invalidate when (one line each)
- **`vector` / `string`**: any reallocation invalidates all; `erase` invalidates from the point on.
  Contiguous, so pointers are offsets — but they move on growth.
- **`deque`**: `push_back`/`push_front` invalidate *iterators* but **not references** to existing
  elements (segmented storage).
- **`list` / `map` / `set` (node-based)**: insert invalidates **nothing**; `erase` invalidates only the
  erased node. Stable references — the reason to pick them when you must hold pointers across mutation.
- **`SmallVector` (the exercise)**: like `vector`, plus the **inline→heap spill** is itself a
  reallocation that invalidates everything — and a **move** of the container invalidates handles into
  it (the inline case relocates elements; §13.4).

## Drills
1. Reproduce §3/§4: take `int* p = &v[0]`, `push_back` past capacity, and show (address + ASan) that
   `*p` is now a heap-use-after-free. Then `reserve` enough up front and show `p` stays valid.
2. From the §2 table, which single call on a `vector` can invalidate references but which cannot ever?
   Contrast with `std::list`. When would you switch container *just* for reference stability?
3. Why does `erase(it)` return an iterator, and how does that let you write a correct
   erase-while-iterating loop? Write the buggy version and the correct version.
4. For the exercise's `SmallVector<T,4>`: you hold `T* p = &sv[0]` while `sv` has 3 inline elements,
   then `push_back` a 4th, then a 5th. After which push does `p` dangle, and why? (§13.4 spill.)

## My summary
