# 11.4 — Exception safety and the strong guarantee

Everything so far converges here. An operation on an owning type can throw partway through (an
allocation fails, an element's copy constructor throws). **Exception safety** is the discipline of
saying — and guaranteeing — what state you're left in when that happens. It is what separates a
container you'd trust with your data from one you wouldn't, and it's the explicit target of the
exercise.

## 1. The four guarantees (Abrahams)

Every operation offers exactly one of these levels, from strongest to weakest:

| Guarantee | Promise if the operation throws |
|---|---|
| **No-throw** (`noexcept`) | it never throws; always succeeds. (Destructors, swap, moves should be here.) |
| **Strong** | **commit-or-rollback**: either it fully succeeds, or it throws and leaves everything *exactly as before*. No observable effect. |
| **Basic** | no leaks, all invariants intact, every object still usable — but the *values* may have changed (partial effect). |
| **None** | all bets off: leaks, broken invariants, UB. Unacceptable. |

Your book reaches for this vocabulary indirectly — it repeatedly prescribes **RAII + well-structured
try/catch** so that "memory is properly released in the context of exceptions" `[MEM ch.2]`. RAII
(§11.1) is what buys you at least the **basic** guarantee for free (no leaks on unwind). The **strong**
guarantee takes deliberate design, below.

## 2. The technique: do the fallible work on the side, then commit with a no-throw step

The recipe for the strong guarantee is always the same shape:
1. do everything that **might throw** on a *separate copy / new buffer*, touching none of the object's
   observable state;
2. then **commit** by swapping the new state in with an operation that **cannot throw** (pointer
   assignments / `swap`).

If step 1 throws, you discard the side work and the original is untouched → rolled back. If you reach
step 2, it can't fail → committed. You saw this in §11.2 §3 (allocate-copy *before* `delete`).

### copy-and-swap: the strong guarantee, packaged
The cleanest expression, and what the exercise's assignment uses:

```cpp
Vector& operator=(const Vector& o) {
    Vector tmp(o);           // step 1: the copy ctor does all the throwing work on a temporary
    swap(tmp);               // step 2: swap is noexcept -> the commit cannot fail
    return *this;            // tmp (our OLD state) is destroyed here
}                            // if the copy threw, we never reached swap -> *this unchanged (strong)
```

It also handles self-assignment correctly without an explicit guard, and reuses the copy ctor and
destructor you already wrote (no duplicated logic). `swap` must be `noexcept` (it only exchanges
pointers/sizes), which is why `noexcept` moves/swaps matter.

## 3. The crux: `std::vector` growth and `std::move_if_noexcept`

A growing vector must, when it runs out of capacity, allocate a bigger buffer and **relocate** the
existing elements into it (Lessons 8 + 10: new raw storage, placement-new each element across, destroy
the old ones). Relocation is the operation that most needs the strong guarantee — and it exposes a
real tension:

- You'd like to **move** each element into the new buffer (fast, §11.3).
- But if an element's move ctor **throws** halfway, you've already gutted some originals (moved-from)
  *and* can't put them back — the old buffer is now partly destroyed. You've lost the strong guarantee.
- If instead you **copy** each element, a throw midway leaves all originals intact (copies don't modify
  the source) — you discard the new buffer and keep the old. Strong guarantee preserved, but slow.

The resolution is `std::move_if_noexcept`, which chooses per type `[CPPREF-move_if_noexcept]`:

> "`std::move_if_noexcept` obtains an rvalue reference to its argument if its move constructor does not
> throw exceptions or if there is no copy constructor (move-only type), otherwise obtains an lvalue
> reference to its argument." `[CPPREF-move_if_noexcept]`
>
> "This is used, for example, by `std::vector::resize`, which may have to allocate new storage and
> then move or copy elements … If an exception occurs during this operation, `std::vector::resize`
> undoes everything it did to this point, which is only possible if `std::move_if_noexcept` was used
> to decide whether to use move construction or copy construction." `[CPPREF-move_if_noexcept]`

So: **`noexcept` move → the vector moves (fast); throwing move → the vector copies (safe).** This is
the concrete payoff of marking your moves `noexcept` (§11.3 §4): it's the difference between a fast
relocation and a slow one, decided automatically.

### (measured) move vs copy on reallocation, on your VM
A `Tracked<NoexceptMove>` type counted across several vector reallocations:
```
noexcept move type:   move noexcept=1  => relocations: copies=0 moves=7
throwing move type:   move noexcept=0  => relocations: copies=7 moves=0
```
Same growth, same element count — but the `noexcept`-move type was **moved** 7 times (0 copies) and
the throwing-move type was **copied** 7 times (0 moves), exactly as `move_if_noexcept` dictates
`(measured; m11.cpp)`.

### (measured) the strong guarantee actually holds
A `Bomb` whose copy throws during a reallocation; the vector must be unchanged:
```
strong guarantee: caught 'boom'; size 3 (was 3) data unmoved=yes v[0]=0 v[2]=2 intact=yes
```
After the throwing relocation the vector still has its 3 original elements, the same backing pointer,
and the right values — the half-built new buffer was discarded and rolled back `(measured; m11.cpp)`.
Your exercise's `reserve` must reproduce exactly this: build the new buffer with `move_if_noexcept`,
and on any exception destroy the partially-built new elements, free the new buffer, and rethrow —
leaving `*this` untouched.

## 4. Where each guarantee belongs
- **Destructors, `swap`, moves**: no-throw (`noexcept`). The whole edifice rests on these not throwing.
  A throwing destructor during unwinding calls `std::terminate` — never do it.
- **`push_back`, `reserve`, copy-assign**: strong, via §2/§3.
- **Multi-element batch ops** (e.g. `insert` in the middle): often only basic — document which.

## Drills
1. Implement copy-assign for the exercise `Vector` with copy-and-swap, then prove the strong guarantee:
   make the element copy throw during `a = b` and check `a` is byte-for-byte unchanged.
2. Write `reserve` two ways — once using `std::move` unconditionally, once using `std::move_if_noexcept`
   — and with a throwing-move element type show which one corrupts the vector on a mid-relocation throw.
3. Why must `swap` be `noexcept` for copy-and-swap to give the *strong* guarantee (not just basic)?
   What level would you get if `swap` could throw?
4. A move-only type (no copy ctor) has a *throwing* move. `move_if_noexcept` still returns an rvalue
   for it (§3 quote). What guarantee does vector growth then offer, and why is that unavoidable?

## My summary
