# 12.2 — Shared ownership and the control block

## 1. What it is

`shared_ptr` lets **several** pointers own one object, and frees the object when the **last** owner
leaves. Your book `[MEM §4.x]`:

> "`std::shared_ptr`: A reference-counted smart pointer that allows multiple owners of a resource. The
> memory is freed only when the last `std::shared_ptr` to the resource is destroyed." `[MEM §4.x]`

cppreference states the destruction rule precisely `[CPPREF-shared]`:

> "Several `shared_ptr` objects may own the same object. The object is destroyed and its memory
> deallocated when … the last remaining `shared_ptr` owning the object is destroyed [or] … is assigned
> another pointer via operator= or reset()." `[CPPREF-shared]`
> "The object is destroyed using delete-expression or a custom deleter." `[CPPREF-shared]`

The mechanism that makes "the last one frees it" work is a **reference count**, and the count lives in
a shared, heap-allocated **control block**.

## 2. The control block

A `shared_ptr` is **two** pointers: one to the object (what `get()`/`*`/`->` use) and one to a
**control block** shared by all `shared_ptr`/`weak_ptr` copies of it. cppreference lists what the
control block holds `[CPPREF-shared]`:

> "The control block is a dynamically-allocated object that holds:
> - either a pointer to the managed object or the managed object itself;
> - the deleter (type-erased);
> - the allocator (type-erased);
> - the number of `shared_ptr`s that own the managed object;
> - the number of `weak_ptr`s that refer to the managed object." `[CPPREF-shared]`

So, stripped to essentials, the control block is:

```cpp
struct ControlBlock {
    std::atomic<long> strong;   // # of shared_ptr owners
    std::atomic<long> weak;     // # of weak_ptr observers (Lesson 12.3)
    // + how to destroy the object (deleter) and the object/pointer itself
};
```

Two objects are allocated for a `shared_ptr<T>(new T)`: the `T` (by your `new`) and the control block
(by the `shared_ptr`). Hold that thought — `make_shared` (§4) fuses them.

## 3. The protocol: ++ on copy, -- on destroy, free at zero

The counting rules are the whole of `shared_ptr`:

- **construct from a raw pointer** → allocate a control block with `strong = 1`, `weak = 0`.
- **copy** (`shared_ptr b = a;`) → both point at the same object & control block; `++strong`.
- **destroy / reset / reassign** → `--strong`; **if it hits 0, destroy the object** (via the deleter).
- the control block itself is freed later, when `weak` also reaches 0 (Lesson 12.3) — it must outlive
  the object so `weak_ptr`s can see "already gone."
- **move** → transfer the two pointers, null the source; **no count change** (ownership moved, not
  shared).

```cpp
template <class T>
class SharedPtr {
    T* p_ = nullptr;
    ControlBlock* cb_ = nullptr;
    void acquire() noexcept { if (cb_) cb_->strong.fetch_add(1); }          // on copy
    void release() noexcept {                                               // on destroy/overwrite
        if (!cb_) return;
        if (cb_->strong.fetch_sub(1) == 1) {        // we were the last strong owner
            delete p_;                               // destroy the OBJECT
            if (cb_->weak.load() == 0) delete cb_;   // and the control block if no weak watchers
        }
    }
  public:
    SharedPtr(const SharedPtr& o) noexcept : p_(o.p_), cb_(o.cb_) { acquire(); }
    ~SharedPtr() { release(); }
    // move, assignment (release-then-acquire), use_count(), get()/*/-> ...
};
```

`use_count()` just reads `strong`. Your book's example walks the count 1 → 2 (copy in an inner scope)
→ 1 (copy leaves) → 0 (`reset`) `[MEM §4.x]`.

> **Refinement used in the exercise (the self weak-ref).** The naïve `release()` above (`if (weak==0)
> delete cb_`) has a reentrancy bug: if the object's *own* destructor drops a `weak_ptr` to its own
> control block, the block can be freed *while* `release()` is still running → use-after-free. The
> standard fix is that the **strong owners collectively hold one weak reference**: the control block
> starts with `weak = 1`, `dec_strong` destroys the object and then does one `dec_weak` to drop that
> self-ref, and the block is freed only when `weak` hits 0. The exercise's provided control block uses
> exactly this — read its comments; you'll see why real `shared_ptr`s carry that extra weak ref. (It
> doesn't change the counting you implement; it just makes the delete-the-block step reentrancy-safe.)

### (measured) use_count, on your VM
```
use_count after make_shared: 1
use_count with a copy in scope: 2
use_count after copy leaves scope: 1
```
A copy raised the count to 2; when the copy left scope its destructor ran `release()` and the count
fell back to 1 `(measured; m12.cpp)` — exactly the protocol above.

## 4. `make_shared`: one allocation instead of two

`SharedPtr<T>(new T(args))` allocates **twice** (object, then control block — §2). `std::make_shared<T>(args)`
puts the object **inside** the control block and allocates **once** `[CPPREF-make_shared]`:

> It "constructs an object of type T and wraps it in a `std::shared_ptr`" using a single allocation for
> both the control block and the object `[CPPREF-make_shared]`.

Benefits: one allocation (faster, less fragmentation — Lesson 7/10), better cache locality (object and
count adjacent). One caveat (derived): because the object lives *in* the control block, its memory
isn't freed until the control block is — i.e. until the last `weak_ptr` also dies (Lesson 12.3). So a
long-lived `weak_ptr` to a `make_shared` object keeps the *object's bytes* (not the object) around.
Prefer `make_shared` by default; know the exception.

## 5. Costs (previewing 12.4)
A `shared_ptr` is **twice the size** of a raw pointer (object + control block pointers), every
copy/destroy is an **atomic** increment/decrement (§12.4), and there's the control-block allocation.
None of that applies to `unique_ptr` (same size as a raw pointer, no count). So the rule is **default
to `unique_ptr`; reach for `shared_ptr` only when ownership is genuinely shared** — not "to be safe."

## Drills
1. Implement `SharedPtr` with the §3 protocol and reproduce the measured `use_count` walk (1→2→1→0).
   Confirm under ASan the object is destroyed exactly once, by the last owner.
2. Add a copy-assignment that is self-assignment-safe and correctly ordered (acquire the new before
   releasing the old, or use copy-and-swap, Lesson 11.4 §2). Test `a = a;` and `a = b;`.
3. Count allocations (hook `operator new`, Lesson 8) for `SharedPtr<T>(new T)` vs a `make_shared`-style
   single-allocation version you write. Confirm 2 vs 1.
4. Why must `release()` read `strong` with an atomic decrement and compare the *previous* value to 1,
   rather than `--strong; if (strong==0)`? (Think two threads, §12.4.)

## My summary
