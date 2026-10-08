# 13.1 — Size, capacity, and geometric growth

## 1. Two numbers, not one

A `std::vector` tracks **size** (how many elements you've put in) and **capacity** (how many slots it
has allocated room for). cppreference `[CPPREF-vector]`:

> "The storage of the vector is handled automatically, being expanded as needed. Vectors usually
> occupy more space than static arrays, because more memory is allocated to handle future growth. This
> way a vector does not need to reallocate each time an element is inserted, but only when the
> additional memory is exhausted." `[CPPREF-vector]`
> "The total amount of allocated memory can be queried using `capacity()`." `[CPPREF-vector]`

So `size() <= capacity()`. The gap is **spare room**: `push_back` while `size() < capacity()` just
placement-news into an existing slot (Lesson 8) — no allocation, no copying. Only when `size() ==
capacity()` does it have to get a bigger buffer. This is the whole reason `vector` is fast.

## 2. Growth is geometric, which makes push_back amortised O(1)

When it must grow, a vector does **not** add one slot — it **multiplies** the capacity by a constant
factor (libstdc++ uses 2×; MSVC 1.5×). Why multiply instead of add? Because the cost model changes
completely:

- **Add 1 each time** (grow N→N+1): the k-th `push_back` copies k elements, so N `push_back`s copy
  `1+2+…+N = O(N²)` elements total. Quadratic — catastrophic. **(derived)**
- **Multiply by 2**: reallocations happen at sizes 1,2,4,…,N, copying `1+2+4+…+N ≈ 2N` elements total
  across *all* N `push_back`s. That's O(N) total work, i.e. **O(1) amortised per `push_back`**
  **(derived — geometric series)**.

cppreference states the result `[CPPREF-push_back]`:

> push_back's complexity is "Amortized constant." `[CPPREF-push_back]`

"Amortised" = any *single* `push_back` that triggers a reallocation is O(current size), but those are
rare enough (geometrically spaced) that the *average* over many calls is O(1). The constant factor
trades space for time: 2× can leave up to ~50% of capacity unused; 1.5× wastes less but reallocates
more often. (There's a subtle argument that a factor < 2, like 1.5, lets freed blocks be reused by
later growths — a Lesson-7 fragmentation consideration — which is why some libraries pick 1.5.)

### (measured) the doubling, on your VM
```
vector<int> capacity growth: 0 -> 1 -> 2 -> 4 -> 8 -> 16 -> 32
```
Each time the vector filled, libstdc++ doubled the capacity `(measured; m13.cpp)`. Seven distinct
capacities carried 32 elements; the reallocations are geometrically spaced exactly as the amortised
analysis predicts.

## 3. A reallocation is expensive — and it moves the elements

One `push_back` that reallocates does a lot (Lesson 11.4 §3): allocate a bigger buffer (Lesson 7),
**relocate every element** into it via `std::move_if_noexcept` (move if the move is `noexcept`, else
copy for the strong guarantee), destroy the old elements, free the old buffer. Two consequences:
- it's O(size) work and touches all the memory (cache, Lesson 6) — which is why you avoid it when you
  can (next section);
- it changes where the elements *live* — the new buffer is at a different address, which is the whole
  story of §13.3 (invalidation).

## 4. `reserve`: pay the growth once, up front

If you know (even roughly) how many elements are coming, `reserve(n)` allocates capacity for `n` in
**one** shot, so the subsequent `push_back`s never reallocate. Your book prescribes exactly this
`[MEM §9.x, §10.x]`:

> "Reserve Capacity for Containers: Containers like `std::vector` allow you to pre-allocate memory by
> calling `reserve()` to minimize reallocations when the container grows." `[MEM §9.x]`
> "pre-allocation, where memory is reserved before it is needed." `[MEM §10.x]`

```cpp
std::vector<int> v;
v.reserve(1000);          // one allocation; the next 1000 push_backs don't reallocate
```

`reserve` only ever *grows* capacity (it never shrinks). To give memory back you use `shrink_to_fit`
(a non-binding request to reduce capacity to size). Both can reallocate, and therefore both invalidate
(§13.3). Rule of thumb: **`reserve` when you can estimate the size** — it's the single easiest vector
speedup, and it also makes the pointer-stability story predictable (no surprise reallocations).

## 5. Through-line
Growth is why `vector` is both fast (amortised O(1), contiguous/cache-friendly) and occasionally
surprising (a reallocation relocates everything). The next section removes the allocation entirely for
the small case (SBO/SSO); the one after shows the invalidation that relocation causes; the exercise's
`SmallVector` lives at the intersection — inline for small, geometric heap growth when it spills.

## Drills
1. Reproduce §2's measurement. Compute, for N=1000 push_backs with 2× growth, the total number of
   element *copies/moves* across all reallocations. Compare to the O(N²) you'd get from grow-by-1.
2. `reserve(1000)` then `push_back` 1000 times, printing `capacity()` — confirm it never changes.
   Now do it without `reserve` and count the reallocations. Time both.
3. libstdc++ doubles; MSVC uses 1.5×. Give one advantage of each (hint: wasted space vs reallocation
   frequency, and the Lesson-7 "can the next growth reuse the freed blocks?" argument for 1.5×).
4. After `reserve(100)` on an empty vector, what are `size()` and `capacity()`? After `shrink_to_fit()`
   on a vector with size 3 / capacity 100, what *may* happen, and why is it only a request?

## My summary
