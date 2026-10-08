# Results

## bench/newdelete_probe — real new/delete behaviour (Lesson 8.1)

Re-run on your machine and paste the output:
```
cmake -S . -B build && cmake --build build --target newdelete_probe && ./build/newdelete_probe
```

Reference run (this VM, GCC 15.2.0 / glibc 2.43, 2026-10-08):
```
__STDCPP_DEFAULT_NEW_ALIGNMENT__ = 16
sizeof(int)=4 sizeof(Noisy)=4 sizeof(D)=1
new Noisy  (expect: operator new, THEN ctor):
  [operator new(4)]
  Noisy() ctor
delete p   (expect: dtor, THEN operator delete):
  Noisy() dtor
  [operator delete sized]
new D[5]   (D has a destructor -> expect a cookie):
  [operator new[](13)]
  5*sizeof(D)=5  operator new[] got=13  => cookie=8 bytes
  [operator delete[] sized]
```

### What each confirms (tie back to the notes)
- `operator new` prints before `Noisy() ctor` → `new` = allocate THEN construct (Lesson 8.1 §1).
- `Noisy() dtor` prints before `operator delete` → `delete` = destruct THEN deallocate (8.1 §2).
- the compiler called the **sized** `operator delete` (8.1 (measured), 8.4 §3).
- `new D[5]` asked for 13 bytes for five 1-byte objects → an **8-byte array cookie** holds the count
  (8.1 §4); this is why `new[]` must be paired with `delete[]`.

## Your counting operator new — notes
- anything surprising in the baseline (allocations the runtime makes before your first `new`)?
- did you hit the recursion or before-main pitfalls (Lesson 8.4 §3)? how did you resolve them?
