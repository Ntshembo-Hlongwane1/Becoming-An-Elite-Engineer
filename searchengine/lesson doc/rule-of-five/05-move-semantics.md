# 05 — Move Semantics

> **The idea in one line:** if the source of a copy is about to be destroyed anyway, don't
> duplicate its resource — **take it**, and leave the source empty.
>
> This turns copying a 1 MB buffer into copying one pointer. It is why C++11 changed the Rule of
> Three into the Rule of Five, and it is the reason `PageGuard` can be returned from `FindLeaf`
> at all: a pin cannot be duplicated, so *move is the only transfer that makes sense*.

---

## 1. Copy versus move, concretely

```cpp
// COPY: allocate a second buffer, duplicate the bytes.  O(n), and it can throw.
String(const String& o)
    : m_Size(o.m_Size), m_Data(new char[o.m_Size + 1]) {
    std::memcpy(m_Data, o.m_Data, m_Size + 1);
}

// MOVE: take their buffer, leave them owning nothing.  O(1), cannot throw.
String(String&& o) noexcept
    : m_Size(o.m_Size), m_Data(o.m_Data) {
    o.m_Data = nullptr;         // <-- THE critical line
    o.m_Size = 0;
}
```

```
   before move:                    after move:

   this            other           this            other
   ┌──────┐        ┌──────┐        ┌──────┐        ┌──────┐
   │ ---- │        │ ptr ─┼──┐     │ ptr ─┼──┐     │ null │
   └──────┘        └──────┘  │     └──────┘  │     └──────┘
                             ▼               ▼
                          [ 1 MB ]        [ 1 MB ]     (same buffer, never copied)
```

**Line 3 of the move constructor is not cleanup — it is the whole correctness argument.** Both
objects now point at the same buffer. Both destructors will run. Exactly one must free it, so
the source must be told it no longer owns anything.

Omit `o.m_Data = nullptr;` and you have rebuilt doc 01's double free, with extra steps.

---

## 2. `std::move` moves nothing

The most misleadingly named function in the standard library:

```cpp
template <typename T>
constexpr std::remove_reference_t<T>&& move(T&& t) noexcept {
    return static_cast<std::remove_reference_t<T>&&>(t);
}
```

It is **a cast**. It generates no code, touches no memory, and moves nothing. It changes the
*value category* of the expression from lvalue to xvalue (doc 03 §2), so that overload
resolution picks the move constructor instead of the copy constructor.

Three consequences that follow directly, and all three bite people:

**The object is not modified by `std::move` itself.**

```cpp
std::move(a);        // a completely untouched -- this statement does nothing at all
String b = std::move(a);   // NOW a is emptied, by the move CONSTRUCTOR
```

The emptying happens in the constructor you wrote, not in `std::move`.

**`std::move` on a copy-only type silently copies.** From doc 03 §4: `const T&` binds rvalues.
So if no move constructor exists, the copy constructor accepts the xvalue and runs. It compiles.
It is correct. It is as slow as a copy, and nothing warns.

**`std::move` on a `const` object silently copies.** A move constructor must modify its source;
`const T&&` cannot bind to `T&&`. This is why `const` local variables and `const` return types
quietly disable moves.

> **Naming aside:** `std::move` should have been called `std::rvalue_cast` or `std::ready_to_move`.
> The name describes intent, not action. Read it as *"I am done with this; you may gut it."*

---

## 3. Move assignment — one more job than the constructor

```cpp
String& operator=(String&& o) noexcept {
    if (this == &o) return *this;       // 1. self-move guard

    delete[] m_Data;                    // 2. release what WE own  <-- the extra job

    m_Data = o.m_Data;                  // 3. steal theirs
    m_Size = o.m_Size;

    o.m_Data = nullptr;                 // 4. and empty them
    o.m_Size = 0;

    return *this;
}
```

Same asymmetry as doc 04: the constructor builds on raw memory and has nothing to release; the
assignment overwrites a live object and **must release first**. Skip step 2 and you leak the
target's buffer on every move assignment.

Measured:

```
=== 5. move assignment ===
  [8] ctor          a
  [9] ctor          b
  [9] MOVE assign <- [8]
  [9] DTOR          a               <- b now holds a's payload
  [8] DTOR          <moved-from>    <- a's destructor runs on an emptied object
```

Note the last line: **the moved-from object is still destroyed.** It is a live object until its
scope ends. That is the constraint the next section is about.

---

## 4. The moved-from state — the rule that matters

> **A moved-from object must be left in a state that is safe to destroy and safe to assign to.**

That is the hard requirement, and it comes straight from the trace above: the destructor *will*
run. `free(nullptr)` and `delete[] nullptr` are both defined no-ops, which is why nulling the
pointer is sufficient.

The standard's term for the state of moved-from standard-library types is **"valid but
unspecified."** Valid means every invariant holds and every operation with no precondition
works. Unspecified means you may not assume *what value* it has.

Measured on libstdc++:

```
string(short) after move: size=0  empty=yes  content=""
string(long)  after move: size=0  empty=yes
vector        after move: size=0  cap=0
unique_ptr    after move: get()=0000000000000000 (guaranteed null)
```

**Do not generalise from three of those four rows.** Only the `unique_ptr` line is guaranteed by
the standard — `unique_ptr` and `shared_ptr` are specified to be null/empty after a move.

The `std::string` rows are **libstdc++ behaviour, not a guarantee.** A short string is stored
inline (small-string optimisation) with no heap buffer to steal, so an implementation is free to
leave the characters in place; MSVC's has historically differed here. Code that reads a
moved-from string is not portable even when it works on your machine.

### What you may and may not do with a moved-from object

| | |
|---|---|
| **Destroy it** | always — this is the hard requirement |
| **Assign a new value to it** | always — this restores a known state |
| **Call operations with no preconditions** | yes: `size()`, `empty()`, `clear()` |
| **Read its value and rely on it** | **no** |
| **Call operations with preconditions** | **no** — `front()` on a moved-from vector is UB |

```
=== a moved-from object is still USABLE (assign a new value) ===
after reassignment: "reassigned"
```

That is the intended pattern: a moved-from object is not poisoned, it is *reset*. Give it a new
value and carry on.

### Self-move

```
=== self-move: legal, but the value is unspecified ===
after self-move: size=0 content=""
```

`s = std::move(s)` left the string empty. Legal, and the value is unspecified — so this is
neither a bug nor something to rely on. GCC helpfully warns:

```
warning: moving 's' of type 'std::string' to itself [-Wself-move]
```

For **your own** types, self-move must at minimum not corrupt or crash. Without the guard in §3,
`delete[] m_Data` frees the buffer and then `m_Data = o.m_Data` copies the dangling pointer back
— the object now holds freed memory. The guard is one comparison; write it. (Or use
copy-and-swap, doc 08, where the problem cannot arise.)

---

## 5. When does a move actually happen?

Only when the source expression is an **rvalue** (doc 03 §7). Measured:

```
=== 3. construction from an RVALUE (std::move) ===
  [4] ctor          a
  [5] MOVE ctor  <-  [4] (source now empty)

=== 6. pass by value (lvalue arg) ===
  [10] ctor          a
  [11] COPY ctor  <-  [10] a          <- lvalue: copy

=== 7. pass by value (rvalue arg) ===
  [12] ctor          a
  [13] MOVE ctor  <-  [12] (source now empty)   <- rvalue: move
```

Same function, same parameter, different value category, different constructor. **The call site
decides**, not the function.

This is why the "sink parameter" idiom from `storage/10` §3 works: `void f(PageGuard g)` costs a
move when called with an rvalue and a copy when called with an lvalue — and for a move-only
type, the lvalue call simply does not compile, forcing the caller to be explicit about the
transfer.

---

## 6. Why move constructors should be `noexcept`

Short version, because doc 07 is entirely about it: `std::vector` will only use your move
constructor during reallocation **if it is `noexcept`**. Otherwise it copies, to preserve the
strong exception guarantee.

Measured, from doc 07:

```
move ctor IS noexcept        copies=0    moves=25
move ctor is NOT noexcept    copies=15   moves=10
```

One keyword, fifteen copies. And a correctly written move constructor genuinely cannot throw —
it only copies pointers and nulls the source — so this is a promise you can actually keep.

---

## 7. Where moves come from that you didn't write

You get moves without typing `std::move` in several places, and recognising them explains a lot
of otherwise-mysterious performance:

- **Returning a local by value** — doc 09. `return guard;` moves (or elides entirely).
- **Passing a temporary** — `f(makeString())`.
- **Container reallocation** — `vector::push_back` growing.
- **`emplace_back`, `push_back(T&&)`** — the rvalue overload.
- **Algorithms** — `std::sort` moves elements rather than copying.

That last one is worth noting: `std::sort` on a `vector<String>` with no move constructor
performs O(n log n) *deep copies*. With one, it shuffles pointers. Same call, wildly different
cost, decided by whether you wrote five lines.

---

## Checkpoint

- [ ] Add move constructor and move assignment to your `String`; verify with `Tracer` that
      moves fire where you expect
- [ ] **Delete the `o.m_Data = nullptr;` line** and run it. You should get doc 01's crash back.
      That line is the whole thing
- [ ] Omit `delete[] m_Data;` from move assignment, run in a loop, watch memory grow
- [ ] Write a move constructor whose member initialiser forgets `std::move` (doc 03 §5) and
      prove with a nested `Tracer` that the member is copied
- [ ] Reproduce the moved-from table; try it with a 5-character string and a 500-character one
- [ ] Answer: *why is `std::move` a cast rather than a function that moves?*
- [ ] Answer: *why must a moved-from object still be destructible, and what does that require of
      your move constructor?*

Next: [06 — The Rules of 0, 3, and 5](06-the-rules.md) — the suppression matrix, and the empty
destructor that silently costs you every move in your program.
