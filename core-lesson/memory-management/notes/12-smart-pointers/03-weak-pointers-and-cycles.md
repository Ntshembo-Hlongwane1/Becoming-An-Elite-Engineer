# 12.3 — Weak pointers and breaking cycles

## 1. The problem `shared_ptr` creates: reference cycles

Reference counting has one classic failure. If object A holds a `shared_ptr` to B, and B holds a
`shared_ptr` back to A, then even when nothing else references them, each keeps the other's strong
count at 1 — so neither ever reaches 0, and both **leak forever**. Your book names it `[MEM §14.2]`:

> "a problem known as circular references can arise when one object references another, which in turn
> references the first object, resulting in memory leaks because the reference count never reaches
> zero, even though the objects are no longer in use." `[MEM §14.2]`

```cpp
struct Node { std::shared_ptr<Node> next; };
auto a = std::make_shared<Node>();
auto b = std::make_shared<Node>();
a->next = b;  b->next = a;     // cycle: a.strong==2? no — a.strong stays >=1 via b->next forever
// scope ends: external shared_ptrs die, but a and b still point at each other -> neither freed
```

### (measured) the leak is real
A `Node` with a destructor that decrements a live counter, on your VM:
```
cycle via shared_ptr (expect NO ~Node):
  leaked this block = 2 node(s)
```
Two nodes allocated, the scope ended, and **neither destructor ran** — both leaked `(measured; m12.cpp)`.
This is not a toy: parent↔child trees, observer lists, and graph structures hit it constantly.

## 2. The fix: `weak_ptr` — reference without owning

A `weak_ptr` points at a `shared_ptr`-managed object **without** contributing to the strong count, so
it never keeps the object alive. Your book `[MEM §4.x, §14.2]`:

> "`std::weak_ptr`: … used to break circular references … It allows access to a resource without
> affecting its reference count, thus preventing memory leaks due to circular ownership." `[MEM §4.x]`

cppreference `[CPPREF-weak]`:

> "`std::weak_ptr` is a smart pointer that holds a non-owning ('weak') reference to an object that is
> managed by `std::shared_ptr`. It must be converted to `std::shared_ptr` in order to access the
> referenced object." `[CPPREF-weak]`
> "it may be used to break reference cycles of `std::shared_ptr`." `[CPPREF-weak]`

Make **one edge of the cycle weak** and the counts can reach zero:

```cpp
struct Node { std::shared_ptr<Node> next; std::weak_ptr<Node> weak_back; };
a->next = b;  b->weak_back = a;   // b observes a without owning it
```

### (measured) the cycle is broken
```
cycle with one weak_ptr edge (expect ~Node for both):
  ~Node(C)
  ~Node(D)
  leaked this block = 0 node(s)
```
Both destructors ran; nothing leaked `(measured; m12.cpp)`. The rule of thumb: in any ownership graph,
**back-references / observer links should be `weak_ptr`**, forward/owning links `shared_ptr`.

## 3. The weak count, and why the control block outlives the object

A `weak_ptr` holds the **control block** (§12.2) but not a strong reference — it bumps the **weak
count** instead. This forces the two-level lifetime from Lesson 12.2 §3:

- **object** destroyed when `strong` reaches 0;
- **control block** destroyed when `strong` *and* `weak` both reach 0.

So after the last `shared_ptr` dies and the object is gone, the control block **survives** as long as
any `weak_ptr` exists — because those `weak_ptr`s need somewhere to read "strong == 0, it's gone."
cppreference's implementation note confirms a `weak_ptr` "stores … a pointer to the control block"
`[CPPREF-weak]`, and the control block persists to hold the weak count even after the object is
destroyed.

## 4. `lock()` and `expired()`: safely turning weak into strong

You can't dereference a `weak_ptr` — the object might already be gone. You ask for temporary ownership
with `lock()`, which **atomically** gives you a `shared_ptr` if the object is still alive, or an empty
one if not. cppreference `[CPPREF-weak]`:

> "`std::weak_ptr` models temporary ownership: when an object needs to be accessed only if it exists,
> and it may be deleted at any time by someone else, `std::weak_ptr` is used to track the object, and
> it is converted to `std::shared_ptr` to acquire temporary ownership." `[CPPREF-weak]`
> `lock` "creates a `shared_ptr` that manages the referenced object"; `expired` "checks whether the
> referenced object was already deleted." `[CPPREF-weak]`

```cpp
if (auto sp = w.lock()) {    // sp is a shared_ptr; strong count bumped for as long as sp lives
    use(*sp);                // guaranteed alive inside this block
} else {
    // the object is gone (w.expired() == true)
}
```

The reason `lock()` must be atomic (not `if (!expired()) return shared_ptr(...);`) is the race in
§12.4: between your `expired()` check and taking ownership, another thread could drop the last strong
reference and destroy the object. `lock()` does "increment strong **only if it's currently nonzero**"
as one atomic step (a compare-exchange loop), closing that window.

### (measured) lock before and after expiry
```
while alive: expired=0 lock()!=null=1
after expiry: expired=1 lock()!=null=0
```
While a `shared_ptr` kept the object alive, `expired()` was false and `lock()` returned a usable
pointer; once the owning `shared_ptr` left scope, `expired()` became true and `lock()` returned empty
`(measured; m12.cpp)`. That empty-on-expiry is exactly what makes `weak_ptr` safe where a raw
back-pointer would be a dangling use-after-free (Lesson 12.5).

## Drills
1. Reproduce the §1 leak and the §2 fix with a counted `Node`. Confirm (count + ASan) that the all-shared
   cycle leaks 2 and the weak-edge version leaks 0.
2. Implement `WeakPtr` with a weak count, `lock()`, and `expired()`. Show the control block is freed
   only after *both* the last `shared_ptr` and the last `weak_ptr` are gone (instrument both deletes).
3. Write `lock()` two ways: the racy `if(!expired()) return SharedPtr(cb);` and the correct
   increment-if-nonzero. Explain the thread interleaving that breaks the first (§12.4).
4. In a parent/children tree, which links should be `shared_ptr` and which `weak_ptr`, and why? What
   breaks if you make the child→parent link `shared_ptr`?

## My summary
