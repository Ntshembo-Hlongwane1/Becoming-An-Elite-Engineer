# 10.3 — The pool (fixed-block) allocator

The arena (§10.2) is fast but can't free one object. The **pool** fixes that for the common case where
all objects are the **same size**: it supports O(1) allocate *and* O(1) free, with zero external
fragmentation, by giving up only the freedom to vary the size.

## 1. The mechanism: a free list of equal blocks

Pre-cut a buffer into `count` blocks of `blockSize` bytes each, and thread the free ones onto a
singly-linked free list. Your book's `FixedBlockAllocator` is exactly this `[MEM §17.x]`:

```cpp
class FixedBlockAllocator {
    FixedBlockAllocator(size_t size, size_t count)
        : blockSize(size), blockCount(count), memory(new char[size * count]) {
        for (size_t i = 0; i < count; ++i)
            freeBlocks.push_back(memory + i * size);     // every block starts free
    }
    void* allocate() {
        if (freeBlocks.empty()) throw std::bad_alloc();
        void* block = freeBlocks.back();                 // pop a free block — O(1)
        freeBlocks.pop_back();
        return block;
    }
    void deallocate(void* block) { freeBlocks.push_back(block); }  // push it back — O(1)
    ~FixedBlockAllocator() { delete[] memory; }
};
```

allocate = pop the free list; deallocate = push onto it. Both are O(1), no search, no split, no
coalesce. The book keeps the free list in a *separate* `std::vector<void*>`; the leaner, classic
design keeps it **intrusive** — store each free block's "next" pointer **inside the free block
itself**, exactly the trick from Lesson 7.2 §2 (a free block's bytes are yours to scribble on):

```cpp
struct FreeNode { FreeNode* next; };     // lives in the first bytes of each FREE block
FreeNode* free_head_;                     // allocate: pop head; deallocate: push head
```

The intrusive version needs **no extra memory** for the free list (it reuses the dead blocks) and no
separate container that could itself allocate — which matters when the pool *is* your allocator. The
exercise uses the intrusive form; a drill compares it to the book's vector version.

## 2. Why there's no fragmentation

Every block is `blockSize` bytes and therefore **interchangeable** — any free block satisfies any
request. So the "enough total free memory but no single piece fits" failure (external fragmentation,
Lesson 7.4 §3) simply cannot happen: if *any* block is free, the next allocation succeeds. You trade
the generality (one size only) for a guarantee `malloc` can't give. There's still mild **internal**
fragmentation — an object smaller than `blockSize` wastes the remainder — which you minimise by sizing
the pool's block to the object.

## 3. The block size must cover the object *and* the free-list node

Two lower bounds on `blockSize` (the exercise enforces both):
- `blockSize >= sizeof(T)` — obviously, the object must fit. Handing out a block smaller than the
  object is a buffer overflow by construction (drill 4 of §10.1).
- `blockSize >= sizeof(FreeNode)` and aligned to `alignof(FreeNode)` — because while a block is
  *free* it stores the intrusive `next` pointer in its first bytes. A block must be big enough to be
  both "an object" (when live) and "a list node" (when free), just like Lesson 7's minimum chunk.
- `blockSize` (and the buffer base) aligned to the object's alignment (Lesson 9), so every block is a
  valid address for `T`.

## 4. Address recycling (the observable signature)

Because `deallocate` pushes the freed block onto the head and `allocate` pops the head, **freeing a
block and immediately allocating returns the same address**. That's the pool recycling memory instead
of going to the OS.

### (measured) on your VM
```
pool: freed 0x...b048, next alloc = 0x...b048  (recycled=yes)
```
The block handed back by the next `allocate` is byte-identical to the one just freed `(measured; m10.cpp)`.
This is also why a pool is the natural home for "object pooling" — recycling game bullets, connection
objects, etc. (`[MEM §9.x]` object pooling). And it mirrors glibc's **fastbins** (Lesson 7.3 §4): a
LIFO list of fixed-size blocks, no coalescing, reused hot — which is both why it's fast and why
(Lesson 7.5 / §10.5) a use-after-free on a pool block silently aliases a live object.

## 5. Pool vs arena vs general — the one-line decisions
- **Arena**: many allocations, all freed together, sizes vary → bump + reset.
- **Pool**: many allocations of **one** size, freed independently → fixed-block free list.
- **General (`malloc`)**: irregular sizes *and* irregular lifetimes → Lesson 7's machinery.

Real allocators layer these: a pool often carves its initial buffer from an arena or `malloc`, and a
general allocator (glibc) is internally a stack of size-class pools (bins) plus a general path.

## Drills
1. Rewrite the book's `FixedBlockAllocator` to use an **intrusive** free list (no `std::vector`).
   Confirm it uses zero extra memory for bookkeeping, then show the §4 address-recycling behaviour.
2. What is the minimum legal `blockSize` for a pool of `struct P { double a, b; }` on a 64-bit machine,
   accounting for both §3 lower bounds? (Give the number and the reasoning.)
3. A pool is full (free list empty). The book throws `std::bad_alloc`; a `pmr` resource would fall
   back to upstream. Which is right for (a) a hard-real-time system, (b) a general library? Why?
4. Explain, using §4 and Lesson 7.5, why a double-`deallocate` on a pool is a double-free primitive:
   what does the free list look like after it, and what does the next `allocate` hand out?

## My summary
