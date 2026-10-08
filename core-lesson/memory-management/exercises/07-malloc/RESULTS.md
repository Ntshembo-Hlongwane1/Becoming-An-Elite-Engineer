# Results

## bench/heap_probe — the real glibc malloc (Lesson 7.1)

Re-run on your machine and paste the output:
```
cmake -S . -B build && cmake --build build --target heap_probe && ./build/heap_probe
```

Reference run (this VM, GCC 15.2.0 / glibc 2.43, 2026-10-08):
```
alignof(max_align_t) = 16
malloc(100): ptr=0x6538a7a14020  break moved 0 B  usable=104  ptr%16=0
malloc(24) x,y stride = 32 B (the chunk size)  usable(x)=24
malloc(200000): break moved 0 B (0 => served by mmap); far from heap=yes
break grew 5136384 B for ~4MB of 64-B requests (sbrk moved the heap up)
malloc(0)=0x...40b0 (unique, freeable); free(NULL) is a no-op
```

### What each line confirms (tie back to the notes)
- `usable=104` for `malloc(100)` → internal fragmentation + 16-rounding (Lesson 7.2 §3).
- `stride = 32` for two `malloc(24)` → the chunk size, not the request (7.2 (measured)).
- `malloc(200000)` moved the break 0 B and lands far from the heap → served by **mmap**, because
  200000 ≥ 128 kB `MMAP_THRESHOLD` (7.1 §4).
- break grew ~5.1 MB for ~4 MB of requests → `sbrk` growth + per-chunk overhead + top slack (7.1 §3).
  (Your exact byte count will vary slightly run to run; the *direction and rough magnitude* are the point.)

## Your allocator — notes

- find_fit policy you chose (first-fit / best-fit) and why:
- anything you had to change to keep `check_invariants()` true after coalescing:
