# 08 — PageGuard

> **Build target:** `internal/kernal/core/storage/PageGuard.hpp` — roughly 90 lines that make
> a leaked unpin *impossible to write* rather than merely detectable. At the end you will
> throw an exception in the middle of a fetch and watch the pin release itself.
>
> **Why this is its own doc.** Docs 09–11 do multi-page operations — a split holds three pages
> at once, a merge holds four, and any of them can throw. Written by hand, every one of those
> functions needs an unwinding path that unpins exactly the pages acquired so far, in the
> presence of early returns. That is unwritable by humans at scale. RAII makes it free and
> automatic, and it is the single highest-leverage C++ technique in this entire series.

---

## 1. The bug you are about to write

Here is doc 09's descent loop, written manually. Read it and find the leak.

```cpp
LeafLocation DiskBPlusTree::FindLeaf(disk_key_t key) {
    page_id_t current = m_Disk.RootPageId();

    while (true) {
        Page* page = m_Pool.FetchPage(current);          // +1 pin
        NodePage node(*page);

        if (node.IsLeaf()) {
            return { current, page };                    // <-- still pinned, intentionally
        }

        const std::size_t idx = node.UpperBoundIndex(key);
        const page_id_t next = node.ChildAt(idx);

        m_Pool.UnpinPage(current, false);                // -1 pin
        current = next;
    }
}
```

That one is actually correct. Now add a validity check, the way you naturally would:

```cpp
        if (node.KeyCount() == 0 && !node.IsLeaf()) {
            throw std::runtime_error("corrupt internal node: no keys");   // LEAK
        }
```

The throw skips the `UnpinPage`. That frame is pinned forever. Nothing crashes, nothing is
detected, and the pool is now one frame smaller for the remainder of the process's life. Do it
in a loop that retries, and the pool degrades to zero usable frames and the engine stops with
"all frames are pinned" — thousands of operations away from the cause.

Now count the exits in a real split (doc 10): three pages held simultaneously, an allocation
that can throw, a disk write that can throw, four early returns. **Every combination needs its
own correct unwind path.** Nobody writes that correctly by hand, and nobody maintains it.

---

## 2. RAII in one sentence

> **Acquire the resource in a constructor, release it in a destructor, and the language will
> release it for you on every path out of the scope — including exceptions.**

C++ guarantees that when a scope exits for *any* reason, destructors of fully-constructed
local objects run, in reverse order of construction. Return, `break`, `throw` — all the same.
This is the one thing C++ does that no garbage-collected language does as well, and a buffer
pool pin is exactly the kind of resource it was designed for.

Same pattern as `std::lock_guard` for mutexes and `std::unique_ptr` for memory. Once you see
that "pin/unpin" is the same shape as "lock/unlock", the design writes itself.

---

## 3. The guard

```cpp
#pragma once
// internal/kernal/core/storage/PageGuard.hpp
#include "BufferPool.hpp"
#include "NodePage.hpp"

// Owns exactly one pin on exactly one page. Move-only. Unpins on destruction.
//
// An empty guard (default-constructed, moved-from, or Drop()ed) owns nothing and its
// destructor does nothing. That state must exist because moving has to leave the source
// harmless, and it is the only reason the null checks below are necessary.
class PageGuard {
public:
    PageGuard() = default;                      // empty

    PageGuard(BufferPool* pool, page_id_t pageId, Page* page)
        : m_Pool(pool), m_Page(page), m_PageId(pageId) {}

    ~PageGuard() { Drop(); }

    // ---- move-only -------------------------------------------------------------
    PageGuard(const PageGuard&)            = delete;
    PageGuard& operator=(const PageGuard&) = delete;

    PageGuard(PageGuard&& other) noexcept
        : m_Pool(other.m_Pool), m_Page(other.m_Page),
          m_PageId(other.m_PageId), m_Dirty(other.m_Dirty) {
        other.Clear();                          // source must not also unpin
    }

    PageGuard& operator=(PageGuard&& other) noexcept {
        if (this != &other) {                   // self-move must not destroy the resource
            Drop();                             // release what we currently hold FIRST
            m_Pool   = other.m_Pool;
            m_Page   = other.m_Page;
            m_PageId = other.m_PageId;
            m_Dirty  = other.m_Dirty;
            other.Clear();
        }
        return *this;
    }

    // ---- access ----------------------------------------------------------------
    bool      Valid()  const { return m_Page != nullptr; }
    page_id_t PageId() const { return m_PageId; }
    Page*     Get()    const { return m_Page; }

    // Typed view. Non-const version marks dirty, because handing out mutating accessors
    // is indistinguishable from being modified -- see section 5.
    NodePage AsNode()          { m_Dirty = true; return NodePage(*m_Page); }
    NodePage AsNodeConst() const { return NodePage(*m_Page); }

    void MarkDirty() { m_Dirty = true; }

    // Release early. Idempotent: calling twice is safe and does nothing the second time.
    void Drop() {
        if (m_Pool != nullptr && m_Page != nullptr) {
            m_Pool->UnpinPage(m_PageId, m_Dirty);
        }
        Clear();
    }

private:
    void Clear() {
        m_Pool   = nullptr;
        m_Page   = nullptr;
        m_PageId = INVALID_PAGE_ID;
        m_Dirty  = false;
    }

    BufferPool* m_Pool   = nullptr;
    Page*       m_Page   = nullptr;
    page_id_t   m_PageId = INVALID_PAGE_ID;
    bool        m_Dirty  = false;
};
```

And two factory methods on the pool:

```cpp
// BufferPool.hpp -- additions
PageGuard FetchGuarded(page_id_t pageId) {
    Page* p = FetchPage(pageId);
    return PageGuard(this, pageId, p);
}

PageGuard NewGuarded(page_id_t& outPageId) {
    Page* p = NewPage(outPageId);
    PageGuard g(this, outPageId, p);
    g.MarkDirty();              // a new page exists only in memory; it MUST reach disk
    return g;
}
```

---

## 3.1 The C++ underneath: rvalues, `std::move`, and the rule of five

Four constructs first appear in that class. They are the machinery the rest of the doc assumes.

### `PageGuard&&` — rvalue references

`Type&` binds to an **lvalue**: something with a name and an address, that will still exist
after this expression. `Type&&` binds to an **rvalue**: a temporary, or something you have
explicitly said you are done with.

```cpp
PageGuard a = pool.FetchGuarded(7);   // FetchGuarded(...) returns an rvalue -- a temporary
PageGuard b = std::move(a);           // std::move(a) makes a act like an rvalue
```

The distinction exists so a constructor can know whether its source will be used again. If the
source is a temporary about to be destroyed, there is no point copying its contents — you can
**steal** them and leave the source empty. That is the entire idea behind move semantics, and
for `PageGuard` it is not an optimisation but the *only* correct behaviour, since the resource
(a pin) cannot be duplicated at all.

> Careful: `F&&` in doc 01 §6.1's `template <typename F> timeIt(F&& fn, ...)` is **not** an
> rvalue reference. When `F` is a deduced template parameter, `F&&` is a *forwarding
> reference* and binds to lvalues too. Same token, different rule, entirely because of
> deduction. `PageGuard&&` on a non-template class is a true rvalue reference.

### `std::move` moves nothing

This is the most misleadingly named function in the standard library.

```cpp
template <typename T>
constexpr std::remove_reference_t<T>&& move(T&& t) noexcept {
    return static_cast<std::remove_reference_t<T>&&>(t);
}
```

It is **a cast**. It generates no code, copies nothing, and moves nothing. All it does is change
the *type* of the expression from lvalue to rvalue, so that overload resolution picks the move
constructor instead of the copy constructor.

Two consequences that follow directly:

- **`std::move` on a type with no move constructor silently copies.** There is no error; the
  cast succeeds and the copy overload is still the best match. For `PageGuard` the copy is
  deleted, so you get a compile error instead — which is the good outcome.
- **The object is not modified by `std::move` itself.** It is modified by whatever
  constructor or assignment operator subsequently runs. `std::move(a); /* no assignment */`
  leaves `a` completely untouched. The emptying happens in the move constructor's
  `other.Clear()`, which is why §4.2's insistence on it is a correctness requirement rather
  than politeness.

### The rule of 0/3/5

There are five special member functions the compiler will generate for you: destructor, copy
constructor, copy assignment, move constructor, move assignment.

**Rule of three** (pre-C++11): if you write any of destructor / copy ctor / copy assign, you
almost certainly need all three — because writing one means the class manages a resource, and
the compiler-generated versions of the others will get it wrong.

**Rule of five** (C++11): add the two move operations.

**Rule of zero** — the one to aim for: *design so you need none of them.* A class whose members
are all self-managing (`std::vector`, `std::unique_ptr`, `std::string`) needs no special
members at all; the compiler-generated ones do the right thing by delegating to the members.

`PageGuard` cannot follow the rule of zero, because a buffer-pool pin is not a resource any
standard type manages. So it declares all five: destructor, copy ctor `= delete`, copy assign
`= delete`, move ctor, move assign. Deleting counts as declaring.

There is a trap in this that costs people real time: **declaring a destructor suppresses the
implicit move operations.** A class with a hand-written destructor and no move constructor is
silently copy-only. If the copy is also deleted, the type becomes unusable in containers with
an error that points nowhere near the cause. Once you write one of the five, write all five —
even if some are `= default`.

### `= default` and `PageGuard() = default;`

`= default` asks the compiler for its generated implementation, explicitly. Here it produces a
guard whose members take their default member initialisers — `m_Pool = nullptr`, `m_Page =
nullptr` — which is precisely the empty state §4.2 requires.

Why `= default` rather than an empty body `PageGuard() {}`? Because the defaulted version can
be *trivial*, which lets the compiler skip work for arrays and value-initialisation, and it
keeps the type's traits (`is_trivially_constructible` and friends) honest. It also states the
intent: *"the default is correct here"*, rather than leaving a reader to check whether an empty
body was deliberate or unfinished.

---

## 4. The four subtleties

### 4.1 Why copying is deleted

If a guard were copyable, two guards would own one pin, and both destructors would unpin it.
The second unpin would either throw (doc 06's double-unpin check) or, worse, release a pin
belonging to someone else — making a live page evictable.

**The resource is not copyable, so the handle must not be either.** This is the same reasoning
as `unique_ptr`. Deleting the copy operations makes the compiler enforce it at every call site.

### 4.2 Why move must clear the source

```cpp
PageGuard a = pool.FetchGuarded(7);
PageGuard b = std::move(a);
// both a and b would unpin page 7 if the move didn't clear a
```

A moved-from object still gets destroyed. Its destructor still runs. So the move must leave it
in a state whose destructor is a no-op — which is exactly what `Clear()` produces.

This is the general rule for move constructors of resource-owning types: **the source must end
up owning nothing.** Not "a valid empty value" as a nicety — as a correctness requirement,
because its destructor is guaranteed to run.

### 4.3 Why move-assignment drops first

```cpp
PageGuard g = pool.FetchGuarded(7);
g = pool.FetchGuarded(9);          // what happens to the pin on 7?
```

Without the `Drop()` on the first line of `operator=`, page 7's pin is overwritten and leaked.
The guard now holds page 9 and no record that 7 was ever pinned.

**Order matters:** drop the old, then take the new. And the `if (this != &other)` guard is not
paranoia — `g = std::move(g)` would otherwise `Drop()` the resource and then copy the
just-cleared members back over itself, producing an empty guard where a valid one was.

### 4.4 Why the move operations are `noexcept`

`std::vector` will only use a move constructor during reallocation if it is `noexcept`.
Otherwise it copies — and our copy is deleted, so the code would simply fail to compile.

More fundamentally: a move that can throw leaves both objects in an indeterminate state, and
there is no way to recover. Our move only copies pointers and clears the source; nothing in it
can throw. Saying so lets the standard library optimise, and it is a promise you can actually
keep.

You will want `std::vector<PageGuard>` in doc 10, for the descent path stack. This one keyword
is what makes that work.

---

## 5. The dirty-tracking design decision

`AsNode()` marks dirty; `AsNodeConst()` does not. That is conservative — you might call
`AsNode()` and only read.

Consider the alternative: track dirtiness precisely by requiring an explicit `MarkDirty()`
after every mutation. From doc 06 §3, the failure modes are wildly asymmetric:

- **Wrongly dirty:** one unnecessary 4096-byte write. Slow, correct.
- **Wrongly clean:** the write is **silently discarded**. Data loss with no error.

So the API should make the safe answer the default and the precise answer opt-in. `AsNode()`
for anything that might write, `AsNodeConst()` for read-only paths where you are certain — and
your descent loop, which is pure reads, uses the const version and pays nothing.

> **A design principle worth generalising:** when two error directions have wildly different
> costs, make the cheap error the *default* and the expensive one require deliberate action.
> This is the same reasoning behind `explicit` constructors and `[[nodiscard]]`.

---

## 6. What the code looks like now

The leaky descent from §1 becomes:

```cpp
LeafGuard DiskBPlusTree::FindLeaf(disk_key_t key) {
    page_id_t current = m_Disk.RootPageId();
    if (current == INVALID_PAGE_ID) return {};

    while (true) {
        PageGuard guard = m_Pool.FetchGuarded(current);
        NodePage  node  = guard.AsNodeConst();

        if (node.KeyCount() == 0 && !node.IsLeaf()) {
            throw std::runtime_error("corrupt internal node");
            // guard's destructor runs during unwinding. The pin is released. Nothing leaks.
        }

        if (node.IsLeaf()) {
            return guard;                  // moved out; the pin transfers to the caller
        }

        current = node.ChildAt(node.UpperBoundIndex(key));
        // guard destructs at the end of this iteration, unpinning the level we just left
    }
}
```

Three things happened and all three are free:

1. **The `UnpinPage` call disappeared.** End of scope handles it.
2. **The throw is safe.** Stack unwinding runs the destructor.
3. **The return transfers ownership.** `return guard;` moves; the caller now owns the pin and
   will release it at *their* end of scope.

Point 3 is worth dwelling on. The guard encodes not just "release this" but *who is currently
responsible for releasing it*, and moving transfers that responsibility. Ownership becomes
something the type system tracks instead of something you comment about.

---

## 7. When to still call `Drop()` explicitly

RAII releases at end of scope, which is sometimes later than you want.

```cpp
void DiskBPlusTree::InsertIntoParent(PageGuard& left, disk_key_t key, PageGuard& right) {
    // ... 200 lines of split handling, holding both guards ...

    left.Drop();
    right.Drop();          // release before recursing -- the parent's split needs frames

    if (parentIsFull) {
        SplitInternal(parentId);      // may itself need several frames
    }
}
```

With a small pool and a deep tree, holding every level pinned during a recursive split can
exhaust the pool. `Drop()` releases early, and because it is idempotent the destructor's later
call is harmless.

**The rule: hold a pin exactly as long as you are actually using the page, and no longer.**
The guard makes the maximum duration safe; it is still your job to make the actual duration
short. A pool of `N` frames supports a tree of depth `N` at best — and much less if splits hold
several pages per level.

---

## Checkpoint

`storage/tests/pageguard_test.cpp`:

```cpp
#include "../PageGuard.hpp"
#include <cassert>
#include <iostream>
#include <vector>

int main() {
    std::remove("guard.db");
    DiskManager dm("guard.db");
    BufferPool  bp(dm, 8);

    page_id_t id;
    { PageGuard g = bp.NewGuarded(id); NodePage(*g.Get()).Init(NodeType::Leaf); }
    assert(bp.PinnedCount() == 0);                       // released at end of scope

    // ---- exception safety: the pin must survive a throw ----
    try {
        PageGuard g = bp.FetchGuarded(id);
        assert(bp.PinnedCount() == 1);
        throw std::runtime_error("boom");
    } catch (const std::exception&) { }
    assert(bp.PinnedCount() == 0);                       // unwinding released it

    // ---- move transfers ownership, does not duplicate it ----
    {
        PageGuard a = bp.FetchGuarded(id);
        assert(bp.PinnedCount() == 1);
        PageGuard b = std::move(a);
        assert(!a.Valid() && b.Valid());
        assert(bp.PinnedCount() == 1);                   // still ONE pin, not two
    }
    assert(bp.PinnedCount() == 0);                       // and exactly one release

    // ---- move-assignment drops the old pin ----
    page_id_t id2;
    { PageGuard tmp = bp.NewGuarded(id2); NodePage(*tmp.Get()).Init(NodeType::Leaf); }
    {
        PageGuard g = bp.FetchGuarded(id);
        g = bp.FetchGuarded(id2);                        // must release id's pin
        assert(bp.PinnedCount() == 1);
        assert(g.PageId() == id2);
    }
    assert(bp.PinnedCount() == 0);

    // ---- Drop is idempotent ----
    { PageGuard g = bp.FetchGuarded(id); g.Drop(); g.Drop(); assert(bp.PinnedCount() == 0); }

    // ---- dirty propagates through the guard ----
    {
        PageGuard g = bp.FetchGuarded(id);
        NodePage n = g.AsNode();                         // non-const: marks dirty
        n.SetKeyCount(1);
        n.SetKeyAt(0, 999);
    }
    bp.FlushPage(id);
    { Page verify; dm.ReadPage(id, verify); assert(NodePage(verify).KeyAt(0) == 999); }

    // ---- guards in a vector: needs noexcept move ----
    {
        std::vector<PageGuard> path;
        for (int i = 0; i < 5; ++i) path.push_back(bp.FetchGuarded(id));
        assert(bp.PinnedCount() == 1);                   // one frame, pin count 5
        path.clear();
        assert(bp.PinnedCount() == 0);
    }

    std::cout << "pageguard_test OK\n";
}
```

Before doc 09, you should have:

- [ ] All of the above passing
- [ ] **The exception test specifically** — it is the whole reason the class exists
- [ ] Deliberately broken it: remove `other.Clear()` from the move constructor and watch the
      double-unpin throw. That is the bug RAII is preventing.
- [ ] Deliberately broken it again: remove `Drop()` from `operator=` and watch `PinnedCount()`
      never return to zero. That is the leak.
- [ ] Removed `noexcept` from the move constructor and read the compiler error from the vector
      test. Understanding that error is worth the five minutes.
- [ ] An answer to: *why must a moved-from guard be left empty rather than merely unspecified?*

Next: [09 — B+Tree I: Descent](09-btree-descent-on-pages.md), where your tree finally meets
your storage layer.
