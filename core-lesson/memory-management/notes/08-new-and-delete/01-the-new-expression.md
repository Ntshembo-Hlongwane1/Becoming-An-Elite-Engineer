# 8.1 — The new-expression: allocate, then construct

## 1. `new` is two operations, not one

Your book introduces `new` the way most courses do — as one thing that "allocates dynamic memory for
an object" and whose partner `delete` frees it `[MEM §2.2]`. True, but it hides the structure that
this whole lesson depends on. cppreference states it precisely:

> "The `new` expression attempts to allocate storage and then attempts to construct and initialize
> either a single unnamed object, or an unnamed array of objects in the allocated storage."
> `[CPPREF-new]`
>
> "The `new` expression allocates storage by calling the appropriate allocation function. If type is
> a non-array type, the name of the function is `operator new`." `[CPPREF-new]`

So `T* p = new T(args)` is really two steps the compiler emits for you **(derived, from the above)**:

```cpp
void* raw = operator new(sizeof(T));   // step 1: get sizeof(T) raw bytes (this is ~malloc, Lesson 7)
T*   p    = ::new (raw) T(args);        // step 2: run T's constructor IN those bytes (placement new, §8.3)
```

Step 1 is just Lesson 7 under a C++ name — `operator new` by default calls `malloc` and returns
suitably-aligned raw storage. Step 2 is what C++ adds over C: it runs a **constructor**, turning raw
bytes into a live object of type `T` (Lesson 2's "object"). Your book names exactly this as the
reason to prefer `new`:

> "`new` and `delete` are preferred in C++ as they support calling constructors and destructors for
> objects." `[MEM §2.3]`

### (measured) the order is observable
With a global `operator new` that prints when called, and a `Noisy` type that prints in its ctor/dtor,
`new Noisy` on your VM prints:
```
new Noisy:
  [operator new(4)]      <- step 1: storage obtained first
  Noisy() ctor           <- step 2: constructor runs second
```
The allocation happens strictly before the constructor. Source: `n1.cpp`. (`sizeof(Noisy)` is 4, so
`operator new(4)` — the allocator is asked for exactly the object's size.)

## 2. `delete` is the mirror: destruct, then deallocate

`delete p` is **not** "free the memory". It is two steps in the opposite order **(derived)**:

```cpp
p->~T();              // step 1: run the destructor (end the object's lifetime, Lesson 2)
operator delete(p);   // step 2: return the raw storage to the allocator (~free, Lesson 7)
```

### (measured) mirror order
```
delete p:
  Noisy() dtor           <- step 1: destructor first
  [operator delete sized]<- step 2: deallocation second
```
Source: `n1.cpp`. (Note it called the *sized* `operator delete(void*, size_t)` — §8.4 §3 explains why
the compiler prefers that form when you provide it.)

This is why `delete` on a base-class pointer to a derived object needs a **virtual destructor**: step
1 must find the *most-derived* destructor, or it destroys only the base sub-object — a classic leak /
UB. (Lesson 11 returns to this; here just notice that step 1 is a real function call the type picks.)

## 3. Contrast with `malloc` (Lesson 7), sharpened
| | `malloc(sizeof(T))` | `new T` |
|---|---|---|
| storage | raw bytes | raw bytes (via `operator new`) |
| lifetime started? | **no** — bytes only; using them as a `T` is UB until you construct | **yes** — constructor ran |
| on failure | returns `nullptr` | **throws `std::bad_alloc`** (§8.2), unless nothrow form |
| partner | `free` | `delete` (destructor + `operator delete`) |

`malloc` gives you Lesson 2's "storage"; `new` gives you a live "object". Mixing the partners is UB:
`free(new T)` skips the destructor and hands a `new` pointer to the wrong allocator; `delete (T*)malloc(...)`
runs a destructor on a never-constructed object. Your book states the rule flatly: "memory allocated
with `new` should be released with `delete`, and memory allocated with `malloc` should be freed with
`free`." `[MEM §2.3]`

## 4. Arrays: `new[]`, `delete[]`, and the cookie

`new T[n]` allocates storage for `n` objects via `operator new[]`, then default-constructs each; `delete[]`
destroys each (in reverse) then calls `operator delete[]` `[CPPREF-new]`. You **must** pair `new[]`
with `delete[]`: your book warns "Failure to do so may result in undefined behavior." `[MEM §2.2]`

Why is it UB and not merely untidy? Because `delete[]` has to know *how many* objects to destroy, and
for a type with a non-trivial destructor the implementation stores that count in extra bytes it
requests from `operator new[]` — the **array cookie**:

> "Array allocation may supply unspecified overhead… The pointer returned by the `new` expression
> will be offset by that value from the pointer returned by the allocation function. Many
> implementations use the array overhead to store the number of objects in the array which is used by
> the `delete[]` expression to call the correct number of destructors." `[CPPREF-new]`

### (measured) the cookie is real
```
new D[5] (D has a destructor):
  [operator new[](13)]     <- asked for 13 bytes for five 1-byte objects
  5*sizeof(D)=5  cookie=8 bytes
```
`sizeof(D)==1`, so the five objects need 5 bytes, but `operator new[]` was asked for **13** — an
**8-byte cookie** holding the count `5` **(derived: 13 − 5 = 8)**. Source: `n1.cpp`. Now the UB is
concrete: call plain `delete` on that pointer and the runtime treats the cookie as part of the object,
destroys the wrong count, and frees a pointer 8 bytes off from what the allocator handed out. (A type
with a *trivial* destructor needs no count, so it often has no cookie — but you must not rely on that;
pair the forms.)

This cookie is the same idea as Lesson 7's chunk header: **metadata the allocator stores just outside
your data so the matching free can do the right thing.** You'll store an analogous header in the
exercise.

## 5. If the constructor throws, the storage is freed for you
A subtle guarantee that makes `new` exception-safe: if step 1 succeeds but step 2 (the constructor)
throws, the already-allocated storage is not leaked —

> "If initialization terminates by throwing an exception … the deallocation function is called to
> free the memory in which the object was being constructed." `[CPPREF-new]`

The compiler pairs each `operator new` form with a matching `operator delete` form for exactly this
rollback. It matters for the exercise: if you replace `operator new`, you must replace the matching
`operator delete`, or a throwing constructor would call the *default* delete on *your* allocation —
a mismatch. §8.4 lists the full set you must keep paired.

## Drills
1. Rewrite `Noisy* p = new Noisy(7);` as the explicit two-step form (§1) using `operator new` and
   placement new. Then write the explicit two-step form of `delete p`. (You'll need §8.3 for the
   placement syntax — come back after reading it.)
2. Reproduce `n1.cpp`. Change `D`'s destructor to `= default` *and* make `D` trivially destructible
   (remove the user dtor). Does `new D[5]` still request a cookie? Explain via §4.
3. Why does `free(new int)` compile but corrupt the heap, while `delete (int*)malloc(4)` compiles but
   is still UB? Name the specific step that goes wrong in each (§3).
4. A class has a `virtual` function but a non-virtual destructor. You `delete base_ptr;` where the
   object is really `Derived`. Which of the two `delete` steps (§2) misbehaves, and what leaks?

## My summary
