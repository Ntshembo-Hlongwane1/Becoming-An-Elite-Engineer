# 12.1 — Unique ownership: `unique_ptr`

## 1. What it is

`unique_ptr` is the smallest possible smart pointer: a raw pointer wrapped in a type that `delete`s it
in its destructor and **cannot be copied**. Your book `[MEM §4.x]`:

> "`std::unique_ptr`: This smart pointer provides exclusive ownership of a resource. When the
> `std::unique_ptr` goes out of scope, the memory it points to is automatically freed. This eliminates
> the need for explicit memory deallocation." `[MEM §4.x]`
>
> "`std::unique_ptr` is a smart pointer that exclusively owns the object it points [to] … Ownership
> can be transferred using the `std::move` function." `[MEM §4.x]`

So it is exactly the Lesson-11 rule-of-five discipline with one rule flipped: **copy is deleted**
(there can be only one owner), and **move steals** the pointer (that's how ownership transfers).

## 2. The whole type, in your head

```cpp
template <class T>
class UniquePtr {
    T* p_ = nullptr;
  public:
    explicit UniquePtr(T* p = nullptr) noexcept : p_(p) {}
    ~UniquePtr() { delete p_; }                         // RAII: free on scope exit (Lesson 11.1)

    UniquePtr(const UniquePtr&)            = delete;    // no copying — one owner only (Lesson 11.2 §4)
    UniquePtr& operator=(const UniquePtr&) = delete;

    UniquePtr(UniquePtr&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }   // move = steal (Lesson 11.3)
    UniquePtr& operator=(UniquePtr&& o) noexcept {
        if (this != &o) { delete p_; p_ = o.p_; o.p_ = nullptr; }      // release, steal, null
        return *this;
    }

    T& operator*()  const { return *p_; }               // use it like a pointer
    T* operator->() const { return p_; }
    T* get()        const noexcept { return p_; }
    explicit operator bool() const noexcept { return p_ != nullptr; }
    // ... release(), reset() — §4
};
```

That's the entire idea. A `unique_ptr` is a pointer that follows the rule of five, with copy deleted.
It has **zero** runtime overhead over a raw pointer — same size, and the generated code is the same
`delete` you'd have written, just impossible to forget (and run on every exit path, Lesson 11.1).

## 3. Copy is deleted, so misuse is a compile error

Because the copy operations are `= delete`d, the compiler *refuses* code that would create a second
owner:

```cpp
UniquePtr<int> a(new int(1));
UniquePtr<int> b = a;            // COMPILE ERROR: copy constructor is deleted
UniquePtr<int> c = std::move(a); // OK: ownership moves; a is now empty (a.get()==nullptr)
```

This is Lesson 11.2 §4's "`=delete` to forbid, not to guess" made into a type: the double-free that a
shallow copy would cause (Lesson 11.2 §1, Lesson 7.5) is turned into a message from the compiler. You
*can't* accidentally get two owners.

## 4. `release`, `reset`, and transferring ownership

Two members express ownership handoff to/from raw pointers:

```cpp
T* release() noexcept { T* t = p_; p_ = nullptr; return t; }   // give up ownership, return the pointer
void reset(T* p = nullptr) noexcept { T* old = p_; p_ = p; delete old; }  // adopt p, free the old
```

- `release()` — "I'm no longer responsible; here's the raw pointer, *you* free it now." The
  `unique_ptr` becomes empty and will not delete.
- `reset(p)` — adopt a new pointer and delete whatever we held. Note the **order**: stash the old
  pointer, overwrite `p_`, *then* delete the old — so `reset(x.get())`-style aliasing and
  self-reset don't delete something still in use (a small Lesson-11 exception-safety habit).

Ownership transfer between `unique_ptr`s is `std::move` (§2's move assignment). You return a
`unique_ptr` from a factory function by value (NRVO/move, Lesson 11.3 §5 — **not** `return
std::move(p)`), and the caller becomes the owner:

```cpp
UniquePtr<Widget> make() { return UniquePtr<Widget>(new Widget); }   // caller owns the result
```

## 5. Custom deleters and arrays (what the real one adds)

The standard `std::unique_ptr` has a second template parameter, the **deleter**, so it can own things
freed by something other than `delete` — a `FILE*` closed with `fclose`, memory from your Lesson-10
pool freed with `pool.deallocate`, etc.:

```cpp
std::unique_ptr<FILE, decltype(&fclose)> f(fopen("x","r"), &fclose);  // closes with fclose, not delete
```

and a partial specialization `std::unique_ptr<T[]>` that uses `delete[]` and `operator[]` for arrays
(Lesson 8.1 §4 — pairing `new[]` with `delete[]`). The exercise builds the scalar `delete` version;
custom deleters and the array form are drills, because they're "same idea, parameterised."

`std::make_unique<T>(args...)` is just `UniquePtr<T>(new T(args...))` wrapped so you never write a
raw `new` (and so it's exception-safe in argument lists) — your book uses it as the default way to
create one `[MEM §4.x]`.

## Drills
1. Implement `UniquePtr` (scalar) and show `UniquePtr<int> b = a;` fails to **compile**, while
   `b = std::move(a)` leaves `a` empty. Confirm under ASan there's no double-free and no leak.
2. Add `release()` and `reset()`. Write a test where `reset(p_)`'s pointer aliases the current one;
   show why the "stash, overwrite, then delete" order (§4) matters.
3. Add a custom-deleter template parameter `D` defaulting to a functor that does `delete`. Make a
   `UniquePtr<FILE, ...>` that `fclose`s. What's the size of the object now vs the default? Why?
4. Why does `std::make_unique<T>(args...)` exist when `UniquePtr<T>(new T(args...))` works? (Hint:
   exception safety when it's one argument among several in a function call — look up the historical
   `f(unique_ptr<T>(new T), g())` evaluation-order leak.)

## My summary
