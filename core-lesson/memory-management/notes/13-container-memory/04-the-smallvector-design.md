# 13.4 — The `SmallVector<T,N>` design

Now assemble the pieces: a vector (geometric heap growth, §13.1) with a small-buffer optimization
(inline storage for `N`, §13.2). This is `llvm::SmallVector` `[LLVM-smallvector]`, and building it is
the exercise. Most of it is the Lesson-11 `Vector` you already wrote; the new material is the inline
buffer and the one operation it breaks: **move**.

## 1. Layout: an inline buffer plus a pointer

```cpp
template <class T, std::size_t N>
class SmallVector {
    alignas(T) std::byte inline_[N * sizeof(T)];   // room for N elements, INSIDE the object (§13.2)
    T*          data_;                              // points at inline_ OR at a heap buffer
    std::size_t size_;
    std::size_t cap_;                               // == N while inline; grows once spilled

    T* inline_ptr() { return reinterpret_cast<T*>(inline_); }
    bool is_inline() const { return data_ == reinterpret_cast<const T*>(inline_); }
};
```

- A fresh `SmallVector` sets `data_ = inline_ptr()`, `size_ = 0`, `cap_ = N`. It owns **no heap** — the
  elements (when added) are placement-new'd into `inline_` (Lesson 8/9: `alignas(T)` keeps it aligned).
- `is_inline()` is just "does `data_` point at my own inline buffer?" — the branch §13.2 §3 warned about.
- Access (`operator[]`, `data`, iteration) goes through `data_` and is identical whether inline or
  heap — callers don't care which.

## 2. Growth: spill from inline to heap

`push_back`/`emplace_back` are the Lesson-11 logic with one change — when `size_ == cap_` you grow via
`reserve`, and `reserve` must handle the **first** growth specially: it allocates a heap buffer, but
the *old* storage might be the inline buffer, which must **not** be freed.

```cpp
void reserve(std::size_t new_cap) {
    if (new_cap <= cap_) return;
    T* nd = alloc_raw(new_cap);                              // heap buffer (operator new, aligned)
    // relocate with move_if_noexcept -> strong guarantee (Lesson 11.4 §3)
    std::size_t i = 0;
    try { for (; i < size_; ++i) ::new (nd + i) T(std::move_if_noexcept(data_[i])); }
    catch (...) { for (std::size_t j = 0; j < i; ++j) nd[j].~T(); free_raw(nd); throw; }
    for (std::size_t j = 0; j < size_; ++j) data_[j].~T();   // destroy old elements
    if (!is_inline()) free_raw(data_);                       // free old buffer ONLY if it was heap
    data_ = nd; cap_ = new_cap;                               // now on the heap
}
```

Once spilled, `is_inline()` is false forever (for this object) — the inline buffer sits unused. That's
fine and is what real `SmallVector`s do.

## 3. The hard part: move cannot steal an inline pointer

For `std::vector`, the move constructor is a three-pointer steal: take the source's buffer pointer,
null the source. For `SmallVector` that is **wrong when the source is inline**, because the source's
`data_` points **into the source object's own `inline_` bytes** — bytes that belong to a different
object and will be destroyed when the source dies. Steal that pointer and you have a dangling pointer
into a dead object's storage.

So the move splits on `is_inline()`:

```cpp
SmallVector(SmallVector&& o) {                 // NOTE: not unconditionally noexcept (inline move can throw)
    if (o.is_inline()) {
        data_ = inline_ptr(); cap_ = N; size_ = 0;
        for (; size_ < o.size_; ++size_)
            ::new (data_ + size_) T(std::move(o.data_[size_]));   // MOVE elements into OUR inline buffer
        o.clear();                                                // destroy the moved-from originals
    } else {
        data_ = o.data_; cap_ = o.cap_; size_ = o.size_;          // heap: steal the buffer pointer
        o.data_ = o.inline_ptr(); o.cap_ = N; o.size_ = 0;         // leave o as an empty inline vector
    }
}
```

Two things to internalise:
- **Inline source ⇒ element-wise move** into the destination's own inline buffer; the destination's
  `data_` points at *itself*, never at the source.
- **Heap source ⇒ pointer steal** (just like `vector`), and the source is reset to its *own* empty
  inline state.

Because the inline branch move-constructs `T`s, a `SmallVector` move is only `noexcept` if `T`'s move
is `noexcept`; in general it can throw. (LLVM's is conditionally `noexcept` for exactly this reason.)
This also means **moving a `SmallVector` invalidates pointers into it** (§13.3) — an inline source's
elements physically change address. Move assignment is the same split, preceded by releasing whatever
`*this` currently holds; copy is the Lesson-11 deep copy (start inline, `reserve` if the source is
bigger than `N`, copy-construct each).

## 4. `capacity()` never drops below `N`
A `SmallVector<T,N>` always has at least `N` capacity (the inline buffer), even when empty and even
after `clear()`. It only exceeds `N` once it has spilled. (There's usually no way to go back to inline
once spilled — matching real implementations; a `shrink`-to-inline is an optional drill.)

## 5. Why build this
`SmallVector` is the type that forces every idea in Phase C to be *correct together*: RAII and the
rule of five (Lesson 11), placement new and alignment (Lessons 8–9), `move_if_noexcept` and the strong
guarantee (Lesson 11.4), and the inline-vs-heap distinction unique to SBO. If your move handles the
inline/heap split correctly and your `reserve` frees the heap buffer but never the inline one, you
understand container memory. The exercise's tests target exactly those two traps.

## Drills
1. Implement the move constructor's two branches. Write a test that moves an **inline** source and
   asserts the destination's `data()` lies inside the *destination* object (not the source), then one
   that moves a **heap** source and asserts the buffer pointer was stolen. (These are in the exercise.)
2. What goes wrong if you implement move as an unconditional pointer-steal (vector-style) and then move
   an inline `SmallVector`? Trace the dangling pointer; predict the ASan error.
3. Why must `reserve` guard `free_raw(data_)` with `if (!is_inline())`? What happens if you `free` the
   inline buffer (which came from the object, not `operator new`)?
4. Give `sizeof(SmallVector<int,8>)` (roughly) and explain each part. For a vector of 10000 tiny
   `SmallVector<int,8>`s that are usually empty, is the inline buffer a win or a waste? When?

## My summary
