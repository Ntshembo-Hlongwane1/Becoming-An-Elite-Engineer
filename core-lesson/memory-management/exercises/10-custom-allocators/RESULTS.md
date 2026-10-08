# Results

## bench/alloc_bench — your Arena / Pool / ArenaResource (Lesson 10)

Build Release and run:
```
cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release && cmake --build build-rel --target alloc_bench
./build-rel/alloc_bench
```

Reference run (this VM, GCC 15.2.0, 2026-10-08):
```
alloc 1000000 x 32B then release:  malloc+free 36.4 ms   arena+reset 2.3 ms  (15.8x)
pool: freed 0x...330 -> next alloc 0x...330  (recycled=yes)
pmr::vector data()=0x...5bc  arena.owns=yes  arena.used=1020 bytes
```

### Reading the numbers
- **arena vs malloc ≈16× here**, while the notes' micro-benchmark (`m10b.cpp`) measured **≈50–75×**.
  Same idea, different number, and the gap is itself a lesson: in `m10b` the arena's `allocate` is
  defined in the same file and the compiler **inlines** it down to a few instructions; here `allocate`
  lives in the `mm` library (a separate translation unit), so it's a real function call a million
  times. Inlining matters. Both runs show the arena dominating because it does ONE `reset()` instead
  of a million `free()`s (Lesson 10.1 §4).
- **pool recycled = yes**: the freed block is handed straight back (Lesson 10.3 §4).
- **arena.owns = yes**: the `std::pmr::vector`'s element storage is inside your arena — a standard
  container running entirely on memory you chose (Lesson 10.4 §2).

## Your allocators — notes
- biggest bug you hit (alignment? free-list direction? the reset/destructor trap?):
- what you'd add to make the pool safe for a security context (Lesson 10.5)?
