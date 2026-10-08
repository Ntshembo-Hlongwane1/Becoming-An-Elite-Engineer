# 10.1 — Why custom allocators: pay only for the generality you use

## 1. The cost of `malloc`'s generality

Lesson 7's allocator — like `malloc` — answers the hardest possible question: *"give me any number of
bytes, and let me free them in any order, forever."* To do that it must, on every call, search a free
list, maybe split a chunk, maybe coalesce on free, and maintain per-chunk metadata (the header). That
machinery costs CPU time, cache misses (chasing free-list links), and memory (headers + fragmentation),
and it is the machinery Lesson 7.5 showed an attacker corrupting.

But most programs don't *need* arbitrary-size, arbitrary-order allocation. They allocate in **patterns**:
- **Same size, many times.** A graph of `Node`s, a particle system, a parser's AST nodes — thousands
  of identically-sized objects. (`[MEM §9.x]`: "if you know that your application frequently creates
  and destroys small objects of the same size, a pool allocator… can improve performance by reusing
  memory blocks instead of constantly requesting and releasing memory from the operating system.")
- **Same lifetime, freed together.** Everything allocated while handling one web request, or rendering
  one frame, dies when that request/frame ends — you never free them individually.

A custom allocator **exploits the pattern** to delete the machinery. If every object is the same size,
you never need to split or coalesce or search — all blocks are interchangeable. If everything dies
together, you never need per-object free at all — you throw the whole buffer away at once. Your book
states the headline benefit: memory pools "preallocate a large block of memory and manage it
internally, reducing allocation overhead and fragmentation." `[MEM §1.4.3]`

## 2. The spectrum: general → pool → arena

Think of allocators on an axis of *how much freedom the caller keeps*, trading freedom for speed:

| Allocator | Sizes | Free individual objects? | Per-op cost | Fragmentation | This lesson |
|---|---|---|---|---|---|
| general (`malloc`, Lesson 7) | any | yes, any order | search + split/coalesce | internal + external | §7 |
| **pool** (fixed-block) | **one** fixed size | **yes**, any order | O(1) push/pop | **none** (external) | §10.3 |
| **arena** (bump/monotonic) | any | **no** — only `reset()` all | O(1) pointer bump | **none** | §10.2 |

Going down the table you give up freedom (one size only; or no individual free) and get speed and
simplicity in return. The art is matching the allocator to the workload: AST nodes → pool; per-frame
scratch → arena; genuinely irregular long-lived objects → keep `malloc`.

## 3. What you must still get right (the parts that don't go away)

A custom allocator is *simpler* than `malloc`, not *lawless*. It still owes its callers everything
Lessons 7–9 established:
- **Alignment** (Lesson 9): every pointer it returns must satisfy the alignment the caller asks for;
  an arena must round its bump pointer up before carving.
- **No overlap**: two live allocations must never share a byte.
- **Lifetime** (Lesson 8): the allocator returns *raw storage*; the caller still placement-news the
  object and destroys it before the storage is reused. An arena `reset()` does **not** run destructors
  — if the objects owned resources, you must destroy them first (Lesson 8.3). This is the #1 custom-arena
  bug and §10.5 revisits it.
- **Backing storage ownership**: it owns one big buffer (from `operator new`/`malloc`/a static array);
  its own destructor frees that, exactly once.

## 4. (measured) why it's worth it

The allocate-many-then-release pattern (an arena's home turf), 1,000,000 × 32-byte objects on your VM:
```
allocate 1000000 x 32B then release:  malloc+free 37–43 ms   arena+reset 0.6–0.7 ms  (≈50–75x faster)
```
`malloc` pays for a general free/coalesce path a million times and then a million individual `free`s;
the arena bumps a pointer a million times and does **one** `reset()`. Same work for the program, ~60×
less time **(measured; `m10b.cpp`)**. The speedup is the generality you *didn't* buy.

(Note: if you instead `malloc`/`free` each object immediately in a tight loop, `malloc` is very fast
too — glibc's per-thread cache recycles the block instantly (Lesson 7.3 tcache). The arena's win shows
on the *bulk-lifetime* pattern, which is exactly when you'd reach for one. Measure your real pattern.)

## 5. Through-line
The next two files build the two workhorses (arena §10.2, pool §10.3), then §10.4 shows how to expose
them to `std::vector` via `std::pmr`. The exercise builds all three. Everything here is the
foundation under Phase C: `std::vector`'s growth, `std::pmr` containers, and (Lesson 12) the control
block of `shared_ptr` are all "an object placement-new'd into storage from *some* allocator."

## Drills
1. For each workload, name the best allocator from §2 and why: (a) a compiler's AST built once then
   walked and freed wholesale; (b) a long-running server's per-connection buffers; (c) a linked list
   of 10⁶ equal-sized nodes with random insert/erase; (d) a std::string that grows unpredictably.
2. Reproduce `m10b.cpp`. Change the pattern to "malloc/free each immediately" (don't store pointers);
   does the arena still win? Explain via tcache (Lesson 7.3) and §4's note.
3. An arena `reset()` reuses the buffer but never ran your objects' destructors. Give a concrete type
   where that leaks (hint: a member that itself owns heap memory), and the Lesson-8 fix.
4. The pool forbids mixed sizes. What specifically breaks if you let a pool hand out a block for an
   object *larger* than its block size? (§10.3 will confirm.)

## My summary
