# 12.4 — Thread safety, costs, and choosing the right pointer

## 1. What is thread-safe about `shared_ptr` — and what is not

This is the most misunderstood point about `shared_ptr`, so state it exactly. cppreference
`[CPPREF-shared]`:

> "All member functions (including copy constructor and copy assignment) can be called by multiple
> threads on **different** `shared_ptr` objects without additional synchronization even if these
> objects are copies and share ownership of the same object." `[CPPREF-shared]`
>
> "If multiple threads of execution access the **same** `shared_ptr` object without synchronization
> and any of those accesses uses a non-const member function of `shared_ptr` then a data race will
> occur." `[CPPREF-shared]`

Unpack it into two guarantees:
- **The reference count is atomic.** Two threads each holding *their own copy* of a `shared_ptr` to the
  same object can copy/destroy their copies concurrently — the strong/weak counts are `std::atomic`,
  so the count stays correct and the object is freed exactly once. This is why the control block uses
  atomic counters (Lesson 12.2) and why `release()` must decrement-and-test atomically (Lesson 12.2
  drill 4): with `--strong; if(strong==0)`, two final owners could both see 0 and double-free, or both
  see nonzero and leak.
- **The object is *not* protected, and neither is a shared `shared_ptr` variable.** `shared_ptr`
  guarantees the object stays *alive* while you hold a copy; it does **nothing** to synchronise
  reads/writes of the object itself (that's your mutex/atomic, Phase D), and two threads mutating *one
  shared* `shared_ptr` variable race like any other shared variable.

So: "`shared_ptr` is thread-safe" means **the ownership bookkeeping is thread-safe**, not the pointee.
A common bug is thinking a `shared_ptr<T>` makes `T` thread-safe — it does not.

## 2. The cost, measured in three currencies

`shared_ptr` is not free, and the costs are why `unique_ptr` is the default:
- **Size:** two pointers (object + control block) vs one for `unique_ptr`/raw. (derived)
- **Time:** every copy and destruction is an **atomic** RMW on the count. Atomics are far cheaper than
  locks but far dearer than a plain increment — and they cause cross-core cache-line traffic
  (Lesson 6.4 false-sharing/coherence) when many threads touch one object's count. Moving a
  `shared_ptr` avoids this (no count change), so pass by `const&` or move, don't copy casually.
- **Allocation:** the control block is a heap allocation (Lesson 7); `make_shared` folds it into the
  object's allocation to pay it once (Lesson 12.2 §4).

`unique_ptr` has **none** of these: same size as a raw pointer, no count, no extra allocation, and its
move is a couple of pointer assignments. It is a zero-overhead abstraction.

## 3. Choosing — the one-line rules
- **`unique_ptr` by default** for any heap object with a clear single owner (most of them). Transfer
  with `std::move`; return by value from factories.
- **`shared_ptr` only when ownership is genuinely shared** — the object's lifetime legitimately
  depends on several independent owners and you can't name one as "the" owner. Your book's use case:
  "an object must remain alive as long as at least one pointer references it" `[MEM §4.x]`.
- **`weak_ptr` for non-owning observers / back-references** (Lesson 12.3): caches, parent pointers,
  observer lists — anything that should *see* the object but not keep it alive.
- **raw pointer / reference for non-owning *parameters*.** A function that just *uses* an object and
  doesn't take ownership should take `T*` or `T&`, not a smart pointer — passing `shared_ptr` by value
  "to be safe" is a needless atomic bump and muddies who owns. (C++ Core Guidelines F.7.)

## 4. `enable_shared_from_this` in one page

Sometimes an object needs to hand out a `shared_ptr` to *itself* (e.g. to register a callback that
keeps it alive). You cannot just write `return SharedPtr<T>(this)` — that creates a **second,
independent control block** for an object that already has one, so two counters race to zero and the
object is double-freed. The fix is `std::enable_shared_from_this<T>` `[CPPREF-esft]`: derive from it,
and the object gets a hidden `weak_ptr` to its own control block (installed when the first
`shared_ptr` is made), so `shared_from_this()` returns a `shared_ptr` sharing the **existing** control
block. Rule: **never build a second `shared_ptr` from a raw `this` (or any raw pointer already owned)** —
use `shared_from_this()` or `make_shared`. (The exercise doesn't build this; recognise the hazard.)

## Drills
1. Two threads each hold their own copy of one `shared_ptr` and repeatedly copy/destroy it in a loop.
   Run under ThreadSanitizer: no data race on the count. Now have two threads write the *same*
   `shared_ptr` variable — TSan should flag it. Explain via §1.
2. Micro-benchmark copying a `shared_ptr` vs a `unique_ptr` move vs a raw-pointer copy, N=10⁷. Where
   does the atomic cost show up, and does pinning both threads to one core vs two change it (Lesson 6.4)?
3. Write the `return shared_ptr<T>(this)` bug, run under ASan, and read the double-free. Then fix it
   with `enable_shared_from_this`. What object does the second control block's existence prove?
4. A function only *reads* a `Widget`. Give its ideal signature and justify why it's neither
   `shared_ptr<Widget>` nor `unique_ptr<Widget>` (§3, ownership vs use).

## My summary
