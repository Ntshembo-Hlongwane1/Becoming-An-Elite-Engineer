# Lesson 13 — Container Memory Behaviour: Growth, SSO, Iterator Invalidation

> Status: complete. Exercise: `../../exercises/13-small-vector/`.
>
> Lessons 11–12 made you the author of the owning types (`Vector`, smart pointers). This lesson is
> about the *memory behaviour* those types exhibit in practice — the behaviour that decides your
> program's speed and its most common memory bug. Three facts run everything: a vector keeps spare
> **capacity** and grows **geometrically** (so `push_back` is amortised O(1) but occasionally
> **reallocates** and copies everything); small containers can keep their data **inline in the object**
> (the **small-buffer / small-string optimization**) to skip the heap entirely; and any reallocation
> **invalidates** every outstanding iterator, pointer, and reference — the source of a huge class of
> real-world use-after-free bugs. You'll fold all three into one type: a **`SmallVector<T,N>`** that
> starts inline and spills to the heap, which is exactly the container LLVM, Chromium, and most
> engines reach for. Building it forces you to confront the one thing `std::vector` never does — a
> move whose source *can't* just hand over a pointer.

This is Phase C, Lesson 3 (ownership), the capstone of the phase. It builds directly on Lesson 11
(the `Vector`, rule of five, `move_if_noexcept`, strong guarantee), Lesson 8 (placement new) and
Lesson 9 (alignment).

## Ground rules recap
Every claim is quoted from a cited source, or marked **(derived)** / **(measured)** (a program run on
your VM — GCC 15.2.0 / libstdc++). SSO is implementation-defined, so it's shown **(measured)**, not
quoted. New sources are at the bottom and in `SOURCES.md`.

## Read in order
1. `01-capacity-and-growth.md` — size vs capacity; geometric growth; why that makes `push_back`
   amortised O(1); `reserve`/`shrink_to_fit`. Measured: capacity `0→1→2→4→8→16→32` (×2 on libstdc++).
2. `02-small-buffer-optimization.md` — keep small data *inside* the object to skip the heap; the
   small-string optimization in `std::string`; the size/branch trade-off. Measured: short string
   cap 15, data inside the 32-byte object, **0** heap allocations.
3. `03-iterator-invalidation.md` — the invalidation rules (reallocation invalidates everything; else
   `push_back` invalidates only `end()`); the dangling pointer it creates (measured) and the UAF link.
4. `04-the-smallvector-design.md` — combine 1+2: `SmallVector<T,N>`; the hard part — a **move** from an
   inline source must *relocate* elements, because its pointer points into the source object itself.
5. `05-security-view.md` — three hats: iterator invalidation as a UAF factory, container OOB, and the
   defences (don't hold pointers across mutation; `.at()`; stable-reference containers).
6. `06-glossary.md`.

Then do the exercise: **a `SmallVector<T,N>` with inline storage** — inline buffer for `N` elements,
spilling to the heap on growth (reusing Lesson 11's strong-guarantee relocation), with a rule of five
whose move/copy correctly **relocate inline elements** instead of stealing a pointer. Tested
(inline→heap spill, element preservation, inline-vs-heap move, deep copy, move-only elements,
leak-freedom) under ASan+UBSan.

## The one-paragraph picture
A `std::vector` stores `size()` elements but allocates `capacity() >= size()` slots, keeping spare
room so most `push_back`s just construct into an existing slot. When it fills up it **reallocates** —
a bigger buffer (libstdc++ doubles), move/copy every element across (`move_if_noexcept`, Lesson 11),
free the old buffer. Because the buffer grows *geometrically*, the total copying over N `push_back`s is
O(N), i.e. **amortised O(1)** each. The catch is that a reallocation moves the elements to a new
address, so **every iterator, pointer, and reference you were holding now dangles** — use one and it's
a use-after-free. A **small-buffer optimization** avoids the heap for the common small case by putting
a fixed-size array *inside the container object itself* and only allocating once the data outgrows it
(`std::string` does this — short strings live in the string object, zero allocations). A
**`SmallVector<T,N>`** combines both: an inline array for `N` elements plus the spill-to-heap growth of
a vector. Its one genuinely hard operation is **move**: when the source still holds its data inline,
the destination can't steal a pointer (that pointer aims into the source's own bytes) — it must
*move-construct the elements* into its own inline buffer and leave the source empty. That single
wrinkle is why writing a `SmallVector` teaches more than writing a `vector`.

## New sources introduced here (also appended to `SOURCES.md`)
| Key | Source |
|---|---|
| `[CPPREF-vector]` | cppreference, `std::vector` — contiguous storage, capacity, the iterator-invalidation table. https://en.cppreference.com/w/cpp/container/vector |
| `[CPPREF-push_back]` | cppreference, `std::vector::push_back` — reallocation invalidation, amortised O(1), strong guarantee. https://en.cppreference.com/w/cpp/container/vector/push_back |
| `[CPPREF-reserve]` | cppreference, `std::vector::reserve` / `capacity`. https://en.cppreference.com/w/cpp/container/vector/reserve |
| `[CPPREF-string]` | cppreference, `std::basic_string` — contiguous storage (SSO is implementation-defined; shown measured). https://en.cppreference.com/w/cpp/string/basic_string |
| `[LLVM-smallvector]` | LLVM Programmer's Manual — `SmallVector<T,N>`: inline storage, spill to heap. https://llvm.org/docs/ProgrammersManual.html#llvm-adt-smallvector-h |
