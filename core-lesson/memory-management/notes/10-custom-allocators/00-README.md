# Lesson 10 — Custom Allocators: Arenas, Pools, and `std::pmr`

> Status: complete. Exercise: `../../exercises/10-custom-allocators/`.
>
> Lesson 7 built a *general* allocator (any size, any order) and paid for that generality with search,
> splitting, coalescing, and metadata an attacker can corrupt. Most real workloads don't need that
> generality — they allocate in **patterns**: thousands of same-sized nodes, or a burst of objects
> that all die at the end of a frame/request. A **custom allocator** exploits the pattern to be
> dramatically faster and simpler: an **arena** that only bumps a pointer (≈50–75× faster than
> `malloc` here, **measured**), a **pool** that hands out fixed-size blocks from a free list with no
> fragmentation. Then C++17's **`std::pmr`** lets you plug such an allocator into `std::vector` and
> friends at *runtime*, without turning the allocator into a viral template parameter. This lesson is
> where "I can write an allocator" (Lesson 7) becomes "I can write the *right* allocator and use it
> with the standard library."

This is Phase B, Lesson 4 (allocation). It builds on Lesson 7 (free lists, chunks), Lesson 8
(`operator new`, placement new), and Lesson 9 (alignment). It is the last allocation lesson before
Phase C (ownership: smart pointers and containers), all of which sit on top of allocators.

## Ground rules recap
Every claim is quoted from a cited source, or marked **(derived)** / **(measured)** (a program run on
your VM — GCC 15.2.0 / glibc 2.43). New sources are listed at the bottom and appended to `SOURCES.md`.

## Read in order
1. `01-why-custom-allocators.md` — the generality cost of `malloc`; allocation *patterns*; the
   spectrum arena → pool → general; what you give up to go faster. Measured: arena 50–75× `malloc`.
2. `02-the-arena-allocator.md` — the bump/monotonic allocator: O(1) allocate, **no per-object free**,
   `reset()` frees everything at once; alignment (Lesson 9); owning the backing buffer; when to use it.
3. `03-the-pool-allocator.md` — fixed-size blocks on a free list: O(1) allocate *and* free, zero
   external fragmentation, address recycling; the free-list-in-the-payload trick again (Lesson 7.2).
   Measured: a freed block is handed straight back.
4. `04-the-allocator-interface-and-pmr.md` — the classic C++ `Allocator` (allocate/deallocate,
   `allocator_traits`, rebind) vs C++17 `std::pmr::memory_resource` + `polymorphic_allocator`;
   `monotonic_buffer_resource`; why pmr beats template allocators. Measured: `pmr::vector` in a buffer.
5. `05-security-view.md` — three hats: custom allocators as a double-edged sword (ASan-blind reuse,
   no hardening) and as a containment tool (arena isolation, wipe-on-reset, guard pages).
6. `06-glossary.md`.

Then do the exercise: **a pool allocator + a `pmr::memory_resource`** — build an `Arena` (bump), a
`PoolAllocator` (fixed-size free list), and wrap the arena as a `std::pmr::memory_resource` you drive
a real `std::pmr::vector` with. Tested under ASan+UBSan.

## The one-paragraph picture
A general allocator answers "give me N bytes" for *any* N in *any* order, which forces it to search,
split, and coalesce (Lesson 7). If your program's allocations follow a pattern, you can replace that
machinery with something trivial. An **arena** (a.k.a. bump/monotonic allocator) keeps one pointer
into a big buffer and, to allocate, rounds it up for alignment and moves it forward — O(1), no
metadata, no search; it cannot free individual objects, only **reset** the whole buffer at once, which
is perfect for per-frame or per-request bursts. A **pool** (fixed-block allocator) pre-cuts a buffer
into equal-sized blocks on a free list; allocate pops the head, free pushes it back — O(1) both ways,
no fragmentation because every block is interchangeable, ideal for many same-sized objects. Both are
tens of times faster than `malloc` on their intended workload. To use them with standard containers,
C++17 adds **`std::pmr`**: a `memory_resource` is an abstract allocator with three virtual functions
(`do_allocate`/`do_deallocate`/`do_is_equal`), and `std::pmr::vector` carries a `polymorphic_allocator`
that forwards to whatever `memory_resource` you hand it at runtime — so one container type works with
an arena, a pool, or the default heap, chosen at run time instead of baked into the type.

## New sources introduced here (also appended to `SOURCES.md`)
| Key | Source |
|---|---|
| `[CPPREF-memres]` | cppreference, `std::pmr::memory_resource` — abstract interface; allocate/deallocate/is_equal forward to private virtual do_*. https://en.cppreference.com/w/cpp/memory/memory_resource |
| `[CPPREF-do_allocate]` | cppreference, `memory_resource::do_allocate` — signature, alignment power-of-two, throws on failure. https://en.cppreference.com/w/cpp/memory/memory_resource/do_allocate |
| `[CPPREF-polyalloc]` | cppreference, `std::pmr::polymorphic_allocator` — runtime-polymorphic Allocator; non-propagating. https://en.cppreference.com/w/cpp/memory/polymorphic_allocator |
| `[CPPREF-monotonic]` | cppreference, `std::pmr::monotonic_buffer_resource` — releases on destruction; deallocate is a no-op; initial buffer + upstream. https://en.cppreference.com/w/cpp/memory/monotonic_buffer_resource |
| `[CPPREF-allocator]` | cppreference, *Allocator* named requirement + `std::allocator_traits`. https://en.cppreference.com/w/cpp/named_req/Allocator |
