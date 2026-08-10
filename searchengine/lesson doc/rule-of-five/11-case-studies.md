# 11 — Case Studies

> Ten docs of theory applied to code you have already written. Each case states the ownership
> model, checks the declarations against doc 06's matrix, and says whether it is correct — and
> two of them are not quite.

---

## 1. `NodePage` — Rule of Zero, and why the deletions are welcome

```cpp
class NodePage {
public:
    explicit NodePage(Page& page) : m_Page(page) {}
    // ... accessors only ...
private:
    Page& m_Page;
};
```

**Ownership model:** non-owning view (doc 01 §6, last row).
**Special members declared:** none.
**Verdict: correct.**

It owns nothing. Copying it produces a second view onto the same page, which is exactly right —
`sizeof(NodePage)` is 8, it is a pointer wearing methods.

The reference member has a consequence worth naming explicitly, from doc 04 §7: **copy
assignment is implicitly deleted**, because a reference cannot be rebound. So:

```cpp
NodePage a(p1);
NodePage b(p2);
a = b;              // does not compile
```

That is a feature. Reassigning a view to point at a different page is the operation that leads
to a view outliving its guard. The compiler forbids it for free.

**The one hazard `NodePage` cannot defend against** is doc 02 §3: it must not outlive the page
it views. Nothing in the type prevents

```cpp
NodePage n = NodePage(*pool.FetchGuarded(id).Get());   // guard dies at the ';'
```

`std::string_view` and `std::span` have exactly this hazard, and the standard's answer is the
same as ours: **pass views down, never store them.**

---

## 2. `DiskManager` — correct, but immovable by accident

```cpp
class DiskManager {
public:
    explicit DiskManager(const std::string& path);
    ~DiskManager();
    DiskManager(const DiskManager&)            = delete;
    DiskManager& operator=(const DiskManager&) = delete;
private:
    std::FILE* m_File = nullptr;
};
```

**Ownership model:** unique ownership of a `FILE*`.
**Declared:** destructor, both copies deleted.
**Verdict: correct, but with an unstated consequence.**

Apply doc 06 Rule S: declaring a destructor **and** copy operations suppresses the implicit
moves. So `DiskManager` is **not movable**:

```cpp
DiskManager MakeManager(const std::string& p) { return DiskManager(p); }   // C++17: OK (elision)
std::vector<DiskManager> managers;
managers.push_back(DiskManager("a.db"));       // does NOT compile
```

The factory works only through C++17's guaranteed elision (doc 09 §1) — no move is required
because no second object exists. Anything needing a genuine move fails.

**Is that wrong?** No — a `DiskManager` holds a file open for the program's lifetime and has no
reason to relocate. But it is currently *accidental*, resting on a rule the next reader may not
know. State it:

```cpp
    DiskManager(DiskManager&&)            = delete;
    DiskManager& operator=(DiskManager&&) = delete;
```

Now all five are declared (Shape C, doc 06 §5), the error message says "use of deleted function"
instead of the confusing "no matching constructor", and nobody has to derive the behaviour from
the suppression rules.

**If you did want it movable**, the implementation is four lines — steal `m_File`, null the
source, and have the destructor tolerate `nullptr` (it already does). Worth doing if you ever
want `DiskManager` in a container.

`BufferPool` is in exactly the same position, for the same reason, with the same fix.

---

## 3. `PageGuard` — Shape A, line by line

```cpp
class PageGuard {
public:
    PageGuard() = default;
    PageGuard(BufferPool* pool, page_id_t pageId, Page* page);
    ~PageGuard() { Drop(); }

    PageGuard(const PageGuard&)            = delete;
    PageGuard& operator=(const PageGuard&) = delete;
    PageGuard(PageGuard&& other) noexcept;
    PageGuard& operator=(PageGuard&& other) noexcept;
};
```

**Ownership model:** unique ownership of a buffer-pool pin.
**Verdict: correct, and now you can say why each line is required.**

| Line | Justified by |
|---|---|
| `~PageGuard() { Drop(); }` | doc 01 — the resource must be released exactly once |
| copies `= delete` | doc 04 §6 — a pin cannot be duplicated; two guards would unpin twice |
| move ctor | doc 05 §1 — the pin must be *transferable*, since `FindLeaf` returns one |
| move assign | doc 05 §3 — `RangeSearch` does `guard = FetchGuarded(next)` in a loop |
| `noexcept` | doc 07 §2 — needed for `std::vector<PageGuard>`, the descent path stack |
| `PageGuard() = default` | doc 05 §4 — the empty state a moved-from guard must land in |

Three specific mechanisms from earlier docs are load-bearing here:

**`other.Clear()` in the move constructor** (doc 05 §1) — without it both guards unpin, and the
second unpin either throws or releases someone else's pin.

**`Drop()` first in move assignment** (doc 05 §3) — without it the guard's current pin is
leaked, and that frame is unusable for the rest of the process.

**`if (this != &other)`** (doc 05 §4) — self-move must not `Drop()` and then copy back the
just-cleared members.

**Should `PageGuard` have `operator*` and `operator->`?** (doc 10 §7.) I would say no. Those make
it look like a smart pointer, and the natural next step for a reader is to store the `Page*`
somewhere — which outlives the guard and defeats the entire design. `unique_ptr` has them
because it *is* a pointer. `PageGuard` is closer to `lock_guard`, which deliberately exposes
nothing.

---

## 4. Your `BPlusTree` — audited

```cpp
    BPlusTree(const BPlusTree&) = delete;
    BPlusTree& operator=(const BPlusTree&) = delete;

    BPlusTree(BPlusTree&& other) noexcept
        : m_Order(other.m_Order), m_Size(other.m_Size), m_Root(other.m_Root) {
        other.m_Size = 0;
        other.m_Root = nullptr;
    }

    BPlusTree& operator=(BPlusTree&& other) noexcept {
        if (this != &other) {
            Clear();
            m_Order = other.m_Order;
            m_Size  = other.m_Size;
            m_Root  = other.m_Root;
            other.m_Size = 0;
            other.m_Root = nullptr;
        }
        return *this;
    }
```

**Verdict: correct.** Every requirement from docs 04–07 is met:

- **`other.m_Root = nullptr`** — the critical line from doc 05 §1. Without it, both trees'
  destructors call `DestroyTree` on the same node graph.
- **`Clear()` before stealing**, in the assignment only — doc 05 §3's extra job. The constructor
  correctly does *not* call it: there is nothing to release on raw memory.
- **Self-move guard** — doc 05 §4.
- **`noexcept` on both**, and honestly so: the moves only copy scalars and null pointers, and
  `Clear()` → `DestroyTree` → `delete` on nodes whose destructors are `std::vector`s, which do
  not throw.
- **Copies deleted**, matching the comment "the tree owns raw node pointers."

One detail worth praising rather than fixing: **`m_Order` is deliberately not reset** on the
source. A moved-from `BPlusTree` therefore has a valid order and an empty root — so it is not
merely "valid but unspecified" (doc 05 §4), it is **fully usable**: you can insert into it
immediately and it will build a new tree. That is a stronger guarantee than the standard library
offers, and it is free.

The only change I would consider: `Clear()` inside a `noexcept` function is a promise that
node destruction cannot throw. True today. If a future `KeyType` had a throwing destructor it
would become false and `std::terminate` would fire. A `static_assert(std::is_nothrow_destructible_v<KeyType>)`
would pin that assumption where it can be checked.

---

## 5. `std::unique_ptr` — the reference implementation

Worth reading as the canonical Shape A, in about 20 lines:

```cpp
template <typename T>
class unique_ptr {
    T* m_Ptr = nullptr;
public:
    explicit unique_ptr(T* p = nullptr) noexcept : m_Ptr(p) {}
    ~unique_ptr() { delete m_Ptr; }

    unique_ptr(const unique_ptr&)            = delete;
    unique_ptr& operator=(const unique_ptr&) = delete;

    unique_ptr(unique_ptr&& o) noexcept : m_Ptr(o.m_Ptr) { o.m_Ptr = nullptr; }

    unique_ptr& operator=(unique_ptr&& o) noexcept {
        if (this != &o) { delete m_Ptr; m_Ptr = o.m_Ptr; o.m_Ptr = nullptr; }
        return *this;
    }

    T& operator*()  const noexcept { return *m_Ptr; }
    T* operator->() const noexcept { return  m_Ptr; }
    explicit operator bool() const noexcept { return m_Ptr != nullptr; }
    T* release() noexcept { T* p = m_Ptr; m_Ptr = nullptr; return p; }
};
```

Every pattern in this series appears: deleted copies, nulling moves, self-assignment guard,
release-before-steal, `noexcept` throughout, `explicit operator bool` (doc 10 §8), and
`operator*`/`operator->` because this one genuinely is a pointer.

`delete nullptr` being a defined no-op is what makes the moved-from state safe — the same reason
your `String`'s `delete[] nullptr` works (doc 05 §4).

---

## 6. `String` — the complete Shape B

The value-like counterpart, with everything from docs 04, 05, and 08:

```cpp
class String {
public:
    String() noexcept = default;
    String(const char* s) : m_Size(std::strlen(s)), m_Data(new char[m_Size + 1]) {
        std::memcpy(m_Data, s, m_Size + 1);
    }
    ~String() { delete[] m_Data; }

    String(const String& o) : m_Size(o.m_Size), m_Data(new char[o.m_Size + 1]) {
        std::memcpy(m_Data, o.m_Data, m_Size + 1);          // DEEP copy
    }
    String(String&& o) noexcept : m_Size(o.m_Size), m_Data(o.m_Data) {
        o.m_Data = nullptr; o.m_Size = 0;                    // steal + empty
    }

    // copy-and-swap: one operator serves both (doc 08 section 3)
    String& operator=(String o) noexcept { swap(*this, o); return *this; }

    friend void swap(String& a, String& b) noexcept {
        std::swap(a.m_Data, b.m_Data);
        std::swap(a.m_Size, b.m_Size);
    }

    std::size_t size() const noexcept { return m_Size; }
    const char* c_str() const noexcept { return m_Data ? m_Data : ""; }

    friend bool operator==(const String& a, const String& b) noexcept {
        return a.m_Size == b.m_Size && std::memcmp(a.m_Data, b.m_Data, a.m_Size) == 0;
    }
private:
    std::size_t m_Size = 0;
    char*       m_Data = nullptr;
};
```

Note `c_str()` returning `""` when `m_Data` is null — a moved-from `String` must still satisfy
its own invariants (doc 05 §4), and "valid" means every precondition-free operation works.

---

## 7. The audit table

| Class | Model | Declared | Verdict |
|---|---|---|---|
| `Page`, `PostingRef`, `RecordID` | value | none | ✔ Rule of Zero |
| `NodePage` | view | none | ✔ Rule of Zero |
| `LeafNode`, `InternalNode` | value-ish (owned by the tree) | none | ✔ — the *tree* owns them |
| `DiskManager` | unique (FILE*) | dtor + deleted copies | ⚠ immovable by accident — state it |
| `BufferPool` | unique (frames) | dtor + deleted copies | ⚠ same |
| `PageGuard` | unique (pin) | all five | ✔ Shape A |
| `BPlusTree` | unique (node graph) | all five | ✔ Shape A |
| `DiskBPlusTree` | **none** — holds references | none | ✔ Rule of Zero |

Two ⚠ rows, one fix each, four lines total.

The last row is worth noticing: `DiskBPlusTree` holds `DiskManager&` and `BufferPool&`. It owns
nothing, so it needs nothing — and its reference members make it non-assignable for free, just
like `NodePage`. **The tree that manages a whole database file needs zero special members**,
because every resource it touches is owned by something else that manages itself. That is the
Rule of Zero working exactly as intended, and it is the strongest argument for the layering in
the storage series.

---

## Checkpoint

- [ ] Add the explicit `= delete` moves to `DiskManager` and `BufferPool`; confirm the error
      message improves
- [ ] Try `std::vector<DiskManager>` before and after; read both errors
- [ ] Write the `static_assert(std::is_nothrow_destructible_v<KeyType>)` in `BPlusTree`
- [ ] Delete `other.m_Root = nullptr;` from your `BPlusTree` move constructor, run your existing
      stress test, and confirm it crashes. Then put it back
- [ ] Implement `String` fully and run it through the harness in doc 12
- [ ] Decide, with reasons, whether `PageGuard` should get `operator*` / `operator->`
- [ ] Answer: *why does `DiskBPlusTree` need no special members despite managing a database?*

Next: [12 — Decision Checklist & Antipatterns](12-checklist.md) — the flowchart, the bug
catalogue, and a reusable test harness.
