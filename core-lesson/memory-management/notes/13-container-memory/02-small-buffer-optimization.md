# 13.2 — The small-buffer / small-string optimization

## 1. The idea: skip the heap for the common small case

Growth (§13.1) assumes the data lives in a heap buffer. But a *lot* of real containers are tiny — a
`string` holding `"id"`, a vector of 2–3 items — and for those a heap allocation (Lesson 7:
`operator new`, a control-block-sized chunk, a potential syscall, a future `free`) is pure overhead,
often dwarfing the work on the data itself.

The **small-buffer optimization (SBO)** — called the **small-string optimization (SSO)** for
`std::string` — avoids it: reserve a small fixed-size array **inside the container object itself**, and
keep the data there while it fits. Only when the data outgrows the inline buffer does the container
allocate on the heap and switch to the §13.1 growth model. For the small case: **zero heap
allocations, and the data sits right next to the object in cache.**

## 2. SSO in `std::string`, measured

The standard doesn't mandate SSO — it only guarantees `string` data is contiguous `[CPPREF-string]`
("The elements of a `basic_string` are stored contiguously") — but every mainstream implementation does
it. On your VM (libstdc++):

### (measured)
```
sizeof(std::string) = 32
short string: capacity=15  data() inside the object = yes (SSO, no heap)
constructing a SHORT string did 0 heap allocation(s); a LONG string did 1
```
`(measured; m13.cpp)` Three things to read off it:
- A `std::string` is **32 bytes** — far bigger than the one pointer a naïve "pointer to heap chars"
  design would need. The extra bytes *are* the inline buffer.
- A short string's `data()` points **inside** the string object (within `[&s, &s + sizeof(s))`), and its
  `capacity()` is **15** — libstdc++'s inline buffer holds 15 chars + a NUL. No heap involved.
- Constructing a short string did **0** allocations; a long one did **1**. The optimization is real and
  measurable: the heap is touched only when the string exceeds the inline buffer.

(Different libraries pick different buffer sizes — libc++'s is larger — but the mechanism is identical.
The number 15 is a libstdc++ fact, not a standard one.)

## 3. The trade-off

SBO/SSO is not free; it trades **space and a branch** for **avoided allocations**:
- **Bigger objects.** The inline buffer lives in the object whether or not it's used, so
  `sizeof(std::string)` is 32, not 8. A `vector<std::string>` of mostly-long strings wastes the inline
  buffers; a `SmallVector<T, 100>` is an enormous object even when empty. Size the inline capacity to
  the *common* small case, not the worst case.
- **A branch on every access.** Every operation must ask "am I inline or on the heap?" (a predictable
  branch, but a branch). Implementations make it cheap, but it's there.
- **Move is no longer a pointer-steal** (the big one — §13.4). If the data is inline, "moving" the
  container can't just copy a pointer, because that pointer points into the source object's own
  storage. It must **move the elements** into the destination's inline buffer. This is the central
  difficulty of the exercise and the reason SSO complicates the rule of five.

The win is large when it matters: map keys, identifiers, small coordinate lists, AST children — all
typically small, all allocation-free with SBO. The guidance: **use a small-buffer container when the
size is usually small and allocation cost dominates**, and pick `N` from the measured common case.

## 4. Where you'll meet it

- `std::string` (SSO), everywhere.
- `llvm::SmallVector<T, N>` / `llvm::SmallString` — pervasive in LLVM/Clang; `[LLVM-smallvector]`
  describes it as a vector with "N elements of inline storage" that only heap-allocates when it grows
  past `N`. Chromium's `absl::InlinedVector`, Boost's `small_vector`, folly's `small_vector` are the
  same idea.
- `std::function`, `std::any` use a related "small object optimization" to store small callables/values
  inline.

The exercise builds exactly this: `SmallVector<T,N>` = §13.1 growth + §13.2 inline buffer.

## Drills
1. Reproduce the SSO measurement. Find your implementation's inline capacity by growing a string one
   char at a time and printing `capacity()`; where does it jump (first heap allocation)?
2. `sizeof(std::string)` is 32 on libstdc++. Sketch what the 32 bytes could hold (pointer, size,
   capacity/buffer union). Why can the buffer and the heap pointer share space (a union)?
3. For `SmallVector<int, N>`, write `sizeof` as a function of `N`. At what `N` does an *empty*
   `SmallVector<int,N>` become larger than, say, a 64-byte cache line (Lesson 6)? What does that imply
   for choosing `N`?
4. Explain in one sentence why SSO makes a `string`'s move constructor unable to be a simple
   pointer-steal for *short* strings (preview of §13.4).

## My summary
