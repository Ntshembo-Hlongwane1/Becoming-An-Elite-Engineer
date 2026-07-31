# 06 — The C++ Implementation

> **Docs 03–05 gave you the algorithms. This doc gives you the C++ around them:** templates,
> comparators, ownership, the rule of five, exception safety, and the API surface. These are
> the things that turn "an algorithm that works in `main()`" into "a header your codebase can
> depend on" — and they're where your `bstree.hpp` currently has real gaps that are worth
> naming before you repeat them.

---

## 1. What `bstree.hpp` gets wrong (and what to carry forward)

Your existing `internal/kernal/core/datastructures/bstree.hpp` is a good learning artifact.
Four things to fix in the B+Tree — each a general C++ lesson, not a nitpick.

### 1.1 No rule of five → double-free waiting to happen

```cpp
class BSTree {
public:
    ~BSTree() { clear(root); }     // has a destructor...
    // ...but no copy ctor, no copy assign, no move ctor, no move assign
};
```

C++ generates a **member-wise copy constructor** for you. So:

```cpp
BSTree a;  a.insert("x");
BSTree b = a;             // b.root == a.root  — SAME POINTER
                          // both destructors run clear() on it → DOUBLE FREE → UB
```

**The Rule of Five (or Zero):** if you write any of {destructor, copy ctor, copy assign, move
ctor, move assign}, you must consider all five. For an owning container, either implement
them all or `= delete` the ones you don't want. §5 does this properly.

### 1.2 Hard-coded `std::string`

`bstree.hpp` only stores strings, and stores no values at all — it's a *set*, not a map. Your
term dictionary needs `string → (offset, length)`. Template it.

### 1.3 Deep recursion in the destructor

`clear()` recurses to the depth of the tree. For a degenerate BST built from sorted input,
that's `N` frames — **stack overflow at ~100k sorted inserts.** The B+Tree is immune (depth
≈ 4), which is a nice side benefit worth noticing.

### 1.4 `recurseInsertion` returns `bool` and always returns `true`

Dead API surface. If a function's return value is never meaningful, remove it — or make it
meaningful. In §4 `insert` returns "was this a new key?", which callers genuinely need.

**Carry forward:** the header-only `#pragma once` style, the naming, and the test structure —
those all match your codebase and should stay.

---

## 2. The template signature

```cpp
template <
    typename Key,
    typename Value,
    typename Compare = std::less<Key>,
    int LEAF_MAX     = 64,      // max ENTRIES in a leaf
    int INTERNAL_MAX = 64       // max CHILDREN in an internal node
>
class BPlusTree;
```

Why each parameter:

- **`Compare` as a type parameter**, defaulted to `std::less<Key>`, matching `std::map`.
  Stored as a member so stateful comparators work. Mark it `[[no_unique_address]]` (C++20) so
  a stateless comparator costs zero bytes.
- **Capacities as non-type template parameters**, not constructor arguments. This makes them
  compile-time constants: the compiler can unroll the in-node loops, and doc 07's fixed-size
  arrays need them at compile time. The cost is that `BPlusTree<K,V,C,4,4>` and
  `BPlusTree<K,V,C,64,64>` are distinct types — which is exactly what you want for testing:
  **`<4,4>` for hand-traceable correctness tests, `<64,64>` or `<128,128>` for benchmarks,
  same code.**
- **Separate leaf and internal capacities** — doc 01 §5. An internal entry is `key + pointer`;
  a leaf entry is `key + value`. If `Value` is large, `LEAF_MAX` should be much smaller.

```cpp
static_assert(LEAF_MAX     >= 2, "LEAF_MAX < 2 cannot satisfy the split invariant");
static_assert(INTERNAL_MAX >= 3, "INTERNAL_MAX < 3 makes internal splits degenerate");

static constexpr int LEAF_MIN     = (LEAF_MAX + 1) / 2;
static constexpr int INTERNAL_MIN = (INTERNAL_MAX + 1) / 2;
```

> **Why `INTERNAL_MAX >= 3`:** with `INTERNAL_MAX = 2` an internal node holds 1 key and
> splits produce halves with 1 child each — below `INTERNAL_MIN = 1`... which technically
> passes but degenerates to a binary tree with extra steps. The `static_assert` documents the
> constraint at the point where violating it would silently produce garbage. Prefer a
> `static_assert` over a comment for anything a caller could get wrong.

---

## 3. Ownership: who deletes what

State the model in a comment at the top of the file. Ambiguity here is how you get
double-frees.

```
 OWNERSHIP MODEL
 ---------------
 - The tree owns `root`.
 - An internal node owns every node in `children`.
 - A leaf owns nothing (its `next` pointer is a BORROWED reference to a sibling
   owned by that sibling's own parent).
 - Therefore: deleting the root recursively deletes every node exactly once,
   and `next` must NEVER be followed during destruction.
```

That last line is the important one and the non-obvious one. The leaf chain creates a *second*
path through the leaves; if `clear()` followed `next` you'd free every leaf twice. Owning
edges are `children` only.

### Why raw pointers here, and not `unique_ptr`

`std::unique_ptr<Node>` in `children` would give automatic cleanup and exception safety for
free. It's a defensible choice for v1. But:

- Split and merge do a lot of **moving nodes between containers**; with `unique_ptr` every one
  of those becomes a `std::move` dance, and `children[i].get()` appears everywhere.
- `unique_ptr` makes `Node` non-trivially-copyable, so doc 08 can't `memcpy` it to a page.
- The tree is a **closed system**: nodes are created only in `splitLeaf`/`splitInternal`/
  `growNewRoot` and destroyed only in `leafMerge`/`internalMerge`/`collapseRootIfNeeded`/
  `clear`. Six places. That's small enough to audit.

**Verdict: raw pointers, with the ownership model documented and the six sites audited.** But
run the test suite under sanitizers — that's not optional:

```bash
g++ -std=c++20 -g -fsanitize=address,undefined bplustree_test.cpp -o bplustree_test
```

ASan catches double-free, use-after-free, and leaks; UBSan catches the out-of-range indexing
that off-by-ones produce. Add this to your `run_test.sh` alongside the existing targets.

---

## 4. The public API

Design it to look like `std::map` where the semantics match, so callers already know it.

```cpp
// ---- capacity -----------------------------------------------------------
bool        empty()  const noexcept;
std::size_t size()   const noexcept;      // O(1) — maintained, not counted
int         height() const noexcept;

// ---- lookup -------------------------------------------------------------
Value*       find(const Key& key);        // nullptr if absent; borrowed, see below
const Value* find(const Key& key) const;
bool         contains(const Key& key) const;

// ---- modify -------------------------------------------------------------
bool insert(const Key& key, const Value& value);   // true = new key, false = overwrote
bool insert(const Key& key, Value&& value);
template <typename... Args>
bool emplace(const Key& key, Args&&... args);
bool erase(const Key& key);                        // true if something was removed
void clear() noexcept;

// ---- ordered access — the reason you're here ---------------------------
iterator begin();
iterator end();
iterator lowerBound(const Key& key);               // first entry >= key
iterator upperBound(const Key& key);               // first entry >  key

template <typename Fn> void scan(const Key& lo, const Key& hi, Fn&& visit);
template <typename Fn> void scanPrefix(const Key& prefix, Fn&& visit);  // Key = string only

// ---- diagnostics (test builds) -----------------------------------------
void validate() const;      // asserts I1..I8 (doc 02 §6)
void printTree(std::ostream& os) const;
```

### Three API decisions worth defending

**`find` returns `Value*`, not `optional<Value>`.** A pointer lets callers mutate in place —
essential when `Value` is a posting-list offset you update, or a `vector` you append to,
without a second descent. The cost is a **borrowed reference**: any subsequent `insert` or
`erase` may split or merge the leaf and reallocate its vectors, dangling the pointer.
Document it exactly as the STL does:

```cpp
// Returns a pointer to the stored value, or nullptr if `key` is absent.
// INVALIDATION: the returned pointer is invalidated by ANY subsequent insert()
// or erase() on this tree. Do not hold it across mutations.
```

**`insert` returns `bool` meaning "was new"**, not `pair<iterator,bool>` like `std::map`.
The iterator half of `map`'s return is rarely used and costs you a leaf pointer + index that
you'd have to keep valid through the split cascade. Simpler is better; add it if a caller
actually needs it.

**`size()` is O(1).** Maintain `entryCount_` in `insert`/`erase`/`clear`. Counting by walking
the leaf chain is `O(N)`, and `size()` is called in loop conditions by unsuspecting callers.
The one rule: **every early return in `insert`/`erase` must get the counter right.** The
overwrite path in `insert` must *not* increment.

---

## 5. Rule of five, done properly

```cpp
    // ---- destructor --------------------------------------------------------
    ~BPlusTree() { clear(); }

    // ---- copy: deep, via the leaf chain (see below) -------------------------
    BPlusTree(const BPlusTree& other)
        : less_(other.less_)
    {
        for (Node* leaf = other.leftmostLeaf(); leaf; leaf = leaf->next)
            for (std::size_t i = 0; i < leaf->keys.size(); ++i)
                insert(leaf->keys[i], leaf->values[i]);
    }

    BPlusTree& operator=(const BPlusTree& other) {
        if (this != &other) {                  // ---- SELF-ASSIGNMENT CHECK ----
            BPlusTree tmp(other);              // copy-and-swap: strong exception safety
            swap(tmp);
        }
        return *this;
    }

    // ---- move: steal the root, leave `other` a valid empty tree -------------
    BPlusTree(BPlusTree&& other) noexcept
        : root_(std::exchange(other.root_, nullptr)),
          height_(std::exchange(other.height_, 0)),
          entryCount_(std::exchange(other.entryCount_, 0)),
          less_(std::move(other.less_)) {}

    BPlusTree& operator=(BPlusTree&& other) noexcept {
        if (this != &other) {
            clear();                           // ---- free OUR nodes first ----
            root_       = std::exchange(other.root_, nullptr);
            height_     = std::exchange(other.height_, 0);
            entryCount_ = std::exchange(other.entryCount_, 0);
            less_       = std::move(other.less_);
        }
        return *this;
    }

    void swap(BPlusTree& o) noexcept {
        std::swap(root_, o.root_);
        std::swap(height_, o.height_);
        std::swap(entryCount_, o.entryCount_);
        std::swap(less_, o.less_);
    }
```

Five points, each a bug you've now avoided:

1. **Self-assignment.** `t = t;` without the check does `clear()` then reads the freed
   `other.root_`. Rare in code, common via aliasing (`v[i] = v[j]`).
2. **Copy-and-swap** for copy-assign gives **strong exception safety** free: if the copy
   throws, `tmp` unwinds and `*this` is untouched.
3. **`std::exchange` for moves** guarantees the moved-from object is a *valid empty tree*,
   not a dangling one. A moved-from object must still be destructible — that's the standard's
   requirement and `exchange` enforces it in one expression.
4. **Move assignment must `clear()` first.** Otherwise you leak your own nodes.
5. **Copy via the leaf chain** is `O(N log N)` (N inserts), not optimal — but it's *correct
   and 5 lines*, and it reuses machinery you've already tested. The `O(N)` version is the
   bulk loader (doc 07 §8) fed from `other`'s leaf chain, which is strictly better once the
   bulk loader exists. Ship the simple one, upgrade later, keep the same tests.

> **The alternative you should consider: `= delete` the copy operations.** A B+Tree over a
> million terms is not something you want copied by accident — an implicit copy in a function
> signature would be a silent performance disaster. Deleting copy and keeping move is a
> defensible, safer design (it's what `unique_ptr` does). If you want copies, name them:
> `BPlusTree clone() const;`.

---

## 6. Exception safety

The question: if `Key`'s copy constructor throws mid-`insert`, is the tree still valid?

The dangerous window is a **split**, which mutates several nodes. Consider `splitLeaf`:

```cpp
right->keys.assign(...);       // (a) may throw — allocation
right->values.assign(...);     // (b) may throw — allocation
leaf->keys.resize(mid);        // (c) cannot throw (shrinking)
leaf->values.resize(mid);      // (d) cannot throw
right->next = leaf->next;      // (e) cannot throw
leaf->next  = right;           // (f) cannot throw
```

If (b) throws, `right` holds keys but no values — **I3 violated**, and `right` leaks. Two
mitigations, in order of value:

**(1) Order operations so all throwing work happens first**, and everything after the last
throw point is `noexcept`. The code above already does this: (a) and (b) are the only throw
points, and they touch only the *new* node. If either throws, `leaf` is completely
unmodified — the tree is still valid, `right` leaks. Then:

**(2) Guard the new node**, so the leak goes away too:

```cpp
std::pair<Key, Node*> splitLeaf(Node* leaf) {
    const int total = (int)leaf->keys.size();
    const int mid   = total / 2;

    auto right = std::make_unique<Node>();     // ---- guarded until we commit ----
    right->isLeaf = true;
    right->keys.assign  (/* [mid, end) */);    // throws → unique_ptr cleans up, tree intact
    right->values.assign(/* [mid, end) */);

    // ---- commit point: nothing below this line can throw ----
    Key sep = right->keys.front();             // (copy — may throw; do it BEFORE commit)
    leaf->keys.resize(mid);
    leaf->values.resize(mid);
    right->next = leaf->next;
    leaf->next  = right.get();
    return { std::move(sep), right.release() };
}
```

`unique_ptr` for the *transient* new node, raw pointer once it's owned by the tree. Best of
both: exception-safe construction, `memcpy`-able node type.

**Realistically:** if `Key = std::string` and `Value = uint64_t`, throws only come from
`bad_alloc`, and if you're out of memory your tree's consistency is the least of your
problems. So: **provide the basic guarantee** (no leaks, no corruption, tree remains valid
though possibly with the insert half-applied), document it, and move on. The strong guarantee
(insert is all-or-nothing) requires a full copy of the modified path and is not worth it here.
Say which guarantee you provide, in a comment. Unstated exception guarantees are a real
maintenance hazard.

---

## 7. Skeleton — the parts you assemble

Here's the frame. The algorithm bodies come from docs 03–05; this shows how they fit and what
the members look like.

```cpp
#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <ostream>
#include <utility>
#include <vector>

// =====================================================================================
//  BPlusTree — ordered map with all data in leaves and leaves linked left-to-right.
//
//  OWNERSHIP: the tree owns root_; an internal node owns its `children`; a leaf owns
//  nothing (`next` is BORROWED). Never follow `next` when destroying.
//
//  INVALIDATION: any insert()/erase() invalidates all iterators and all Value* from find().
//
//  EXCEPTION SAFETY: basic guarantee. On throw the tree remains a valid B+Tree, but a
//  partially applied insert may be visible. No leaks.
//
//  INVARIANTS I1..I8 are documented in lesson doc/bplustree/02-anatomy-and-invariants.md
//  and checked by validate() when BPTREE_VALIDATE is defined.
// =====================================================================================
template <typename Key,
          typename Value,
          typename Compare = std::less<Key>,
          int LEAF_MAX     = 64,
          int INTERNAL_MAX = 64>
class BPlusTree {
    static_assert(LEAF_MAX     >= 2, "LEAF_MAX must be >= 2");
    static_assert(INTERNAL_MAX >= 3, "INTERNAL_MAX must be >= 3");

public:
    static constexpr int LEAF_MIN     = (LEAF_MAX + 1) / 2;
    static constexpr int INTERNAL_MIN = (INTERNAL_MAX + 1) / 2;

private:
    // ---------------------------------------------------------------- Node
    struct Node {
        bool               isLeaf = false;
        std::vector<Key>   keys;

        std::vector<Node*> children;   // internal only: size() == keys.size() + 1   (I2)
        std::vector<Value> values;     // leaf only:     size() == keys.size()       (I3)
        Node*              next = nullptr;   // leaf only: right sibling             (I7)

        explicit Node(bool leaf) : isLeaf(leaf) {}
    };

    Node*       root_       = nullptr;
    int         height_     = 0;          // 0 == empty; 1 == root is a leaf
    std::size_t entryCount_ = 0;
    [[no_unique_address]] Compare less_{};

    // ------------------------------------------------- comparison helpers (doc 03 §3)
    bool lt(const Key& a, const Key& b) const { return less_(a, b); }
    bool eq(const Key& a, const Key& b) const { return !less_(a, b) && !less_(b, a); }

    // --------------------------------------------------- in-node search (doc 03 §2)
    int lowerBoundIdx(const std::vector<Key>& ks, const Key& k) const;
    int upperBoundIdx(const std::vector<Key>& ks, const Key& k) const;

    // ------------------------------------------------------- occupancy (doc 04 §2, 05 §3)
    static bool leafOverfull    (const Node* n) { return (int)n->keys.size()     >  LEAF_MAX;     }
    static bool internalOverfull(const Node* n) { return (int)n->children.size() >  INTERNAL_MAX; }
    static bool leafUnderfull   (const Node* n) { return (int)n->keys.size()     <  LEAF_MIN;     }
    static bool internalUnderfull(const Node* n){ return (int)n->children.size() <  INTERNAL_MIN; }

    // ------------------------------------------------------------ structure ops
    Node* leftmostLeaf() const { Node* n = root_; while (n && !n->isLeaf) n = n->children.front(); return n; }
    Node* descendToLeaf(const Key& k) const;                       // doc 03 §4

    std::pair<Key, Node*> splitLeaf(Node* leaf);                   // doc 04 §3
    std::pair<Key, Node*> splitInternal(Node* node);               // doc 04 §4
    void growNewRoot(Key sep, Node* oldRoot, Node* newRight);      // doc 04 §5

    void leafBorrowLeft(Node* p, int i);                           // doc 05 §4.1
    void leafBorrowRight(Node* p, int i);                          // doc 05 §4.2
    void leafMerge(Node* p, int j);                                // doc 05 §4.3
    void internalBorrowLeft(Node* p, int i);                       // doc 05 §4.4
    void internalBorrowRight(Node* p, int i);                      // doc 05 §4.5
    void internalMerge(Node* p, int j);                            // doc 05 §4.6
    void repairChild(Node* p, int i);                              // doc 05 §7
    void collapseRootIfNeeded();                                   // doc 05 §5

    std::optional<std::pair<Key, Node*>>
         insertInto(Node* n, const Key& k, const Value& v, bool& inserted);  // doc 04 §6.2
    bool removeFrom(Node* n, const Key& k);                        // doc 05 §7

    // ------------------------------------------------------------ teardown
    // Follows `children` ONLY. Never `next` — that would double-free. (§3)
    static void destroySubtree(Node* n) {
        if (!n) return;
        if (!n->isLeaf)
            for (Node* c : n->children) destroySubtree(c);
        delete n;
    }

public:
    BPlusTree() = default;
    explicit BPlusTree(Compare c) : less_(std::move(c)) {}

    ~BPlusTree() { clear(); }
    BPlusTree(const BPlusTree&);
    BPlusTree& operator=(const BPlusTree&);
    BPlusTree(BPlusTree&&) noexcept;
    BPlusTree& operator=(BPlusTree&&) noexcept;
    void swap(BPlusTree&) noexcept;

    bool        empty()  const noexcept { return entryCount_ == 0; }
    std::size_t size()   const noexcept { return entryCount_; }
    int         height() const noexcept { return height_; }

    void clear() noexcept {
        destroySubtree(root_);
        root_ = nullptr; height_ = 0; entryCount_ = 0;
    }

    Value* find(const Key& key);
    bool   contains(const Key& key) const { return const_cast<BPlusTree*>(this)->find(key); }
    bool   insert(const Key& key, const Value& value);
    bool   erase(const Key& key);

    class iterator;                                                // doc 03 §7
    iterator begin();
    iterator end();
    iterator lowerBound(const Key& key);

    template <typename Fn> void scan(const Key& lo, const Key& hi, Fn&& visit);

#ifdef BPTREE_VALIDATE
    void validate() const;                                         // doc 02 §6
#else
    void validate() const {}
#endif
    void printTree(std::ostream& os) const;                        // doc 09 §2
};
```

---

## 8. Details that bite

### 8.1 `int` vs `std::size_t` in index arithmetic

```cpp
for (int i = (int)node->keys.size() - 1; i >= 0; --i)   // fine
for (size_t i = node->keys.size() - 1; i >= 0; --i)     // INFINITE LOOP when size()==0
```

`size_t` is unsigned: `0 - 1` wraps to `SIZE_MAX`, and `i >= 0` is always true. B+Tree code is
full of `i - 1` (the left separator is `keys[i-1]`) and `mid - 1`, so **use `int` for all
index arithmetic** and cast at the vector boundary. Compile with `-Wall -Wextra
-Wsign-compare` and fix every warning — this is exactly the class of bug it catches.

### 8.2 Iterator invalidation is total

`std::map` guarantees iterators survive insertion of *other* elements — node-per-element makes
that free. A B+Tree cannot: a split moves half a leaf's entries to a new node, so an iterator
holding `(leaf, idx)` now points at the wrong entry. **All iterators and all `Value*` from
`find` are invalidated by any mutation.** Document it loudly; it's the one place your API
differs from the `std::map` shape it otherwise mimics, and callers *will* assume otherwise.

### 8.3 `const` correctness needs two descents

`find` and `const find` differ only in return type, but both need `descendToLeaf`. Make the
private helpers `const` and returning `Node*` (the *node* isn't logically const even in a
const tree — you're returning a pointer into it), then:

```cpp
const Value* find(const Key& key) const {
    return const_cast<BPlusTree*>(this)->find(key);   // Meyers' idiom — safe direction
}
```

Casting away const to call the non-const version is safe *in this direction* (the non-const
version doesn't mutate). The reverse would be UB. Only ever write it this way round.

### 8.4 `[[no_unique_address]]` and empty comparators

`std::less<Key>` is empty (no members). Without `[[no_unique_address]]` it still occupies 1
byte, and padding may cost 8. With it, zero. C++20. If you're on C++17, use private
inheritance from `Compare` (the empty base optimisation) or just accept the byte.

### 8.5 Where does `height_` get updated?

Exactly two places: `growNewRoot` (+1) and `collapseRootIfNeeded` (−1). Plus the empty-tree
special case in `insert` (0 → 1). If it's updated anywhere else, that's a bug. Assert it in
the validator: walk to the leftmost leaf counting levels and compare against `height_`.

### 8.6 Don't `assert` on user input

`assert(root_ != nullptr)` in `find` is wrong — an empty tree is a legal state and `find`
should return `nullptr`. Asserts encode **your** invariants (I1–I8), not the caller's
behaviour. A caller passing a key that isn't there is not a bug.

---

## 9. Build integration

Match your existing structure exactly:

```
internal/kernal/core/datastructures/
    bstree.hpp
    ringbuffer.hpp
    bplustree.hpp                  ← new
    tests/
        bstree_test.cpp
        ringbuffer_test.cpp
        bplustree_test.cpp         ← new
        run_test.sh                ← add a target
```

Your test style (from `tests/bstree_test.cpp`) is: one `void test_x()` per case, `assert`,
`[PASSED]` print, all called from `main`. **Keep it** — consistency in a codebase beats
"better" frameworks that only one file uses. Doc 09 §3 gives the suite in that style.

Compile with validation and sanitizers on for tests:

```bash
g++ -std=c++20 -Wall -Wextra -Wsign-compare -g -O0 \
    -DBPTREE_VALIDATE \
    -fsanitize=address,undefined \
    bplustree_test.cpp -o bplustree_test.exe
```

and a separate release build **without** `-DBPTREE_VALIDATE` for the benchmarks in doc 07 —
running the O(N) validator inside a benchmark loop turns your `O(log N)` insert into `O(N)`
and produces meaningless numbers. That's a mistake people make once.

---

## 10. Checkpoint before doc 07

1. Copy `BSTree` with `BSTree b = a;`. Walk through exactly what goes wrong and when. (§1.1)
2. Why are the capacities template parameters rather than constructor arguments? Name a
   concrete benefit for testing. (§2)
3. Why must `clear()` never follow `next`? (§3)
4. `insert` has three return paths. Which must not touch `entryCount_`? (§4)
5. Why does copy-assignment use copy-and-swap, and what guarantee does it buy? (§5)
6. In `splitLeaf`, which lines can throw? What must be true of everything after them? (§6)
7. Write the `for` loop from §8.1 with `size_t` and explain the infinite loop. (§8.1)

**Build now:** the full header — assemble the algorithm bodies from docs 03–05 into the
skeleton in §7, with the rule of five from §5 and the ownership comment from §3. Compile with
`-Wall -Wextra -Wsign-compare -fsanitize=address,undefined -DBPTREE_VALIDATE`, then run doc
04's and doc 05's checkpoint tests against it. **Zero warnings, zero sanitizer reports, all
tests green** — that's the bar before you go near performance work.
