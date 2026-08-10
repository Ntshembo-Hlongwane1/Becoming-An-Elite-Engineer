# 04 — Copy Semantics

> **Build target:** a correct `String` class — the fix for doc 01's `BrokenString`. Two of the
> five members, written properly, plus the trap that makes copy *assignment* harder than copy
> *construction*.
>
> The trap is self-assignment, and §5 runs code that survives it and returns **garbage** rather
> than crashing. Silent wrong answers again.

---

## 1. Construction vs assignment — two different jobs

This distinction governs everything in this doc and the next.

```cpp
String b = a;      // COPY CONSTRUCTION -- b does not exist yet
b = a;             // COPY ASSIGNMENT   -- b already exists and owns something
```

| | Copy constructor | Copy assignment |
|---|---|---|
| Target state | **raw memory**, no object yet | a **live object** owning resources |
| Must acquire the new resource | yes | yes |
| Must release the old resource | **no — there isn't one** | **yes** |
| Must handle self-reference | no (can't happen) | **yes** |
| Return type | none | `T&` |

The middle two rows are why assignment is the harder one and where the bugs live.

Measured, from doc 02's `Tracer`:

```
=== 4. copy ASSIGNMENT vs copy CONSTRUCTION ===
  [6] ctor          a
  [7] ctor          b               <- b exists already
  [7] COPY assign <- [6] a          <- assignment, not construction
  [7] DTOR          a-copyassigned
  [6] DTOR          a
```

Two constructions, one assignment, two destructions. Compare with construction:

```
=== 2. copy construction from an LVALUE ===
  [2] ctor          a
  [3] COPY ctor  <-  [2] a          <- one construction, no prior object
```

**If you find yourself unsure which is running, the `Tracer` will tell you.** The distinction
is invisible at the call site — `String b = a;` and `b = a;` look nearly identical — and it
changes which function executes.

---

## 2. The copy constructor

```cpp
String(const String& other) {
    m_Size = other.m_Size;
    m_Data = new char[m_Size + 1];        // OUR OWN buffer
    std::memcpy(m_Data, other.m_Data, m_Size + 1);
}
```

Three things about the signature are not negotiable.

### It takes a reference — by value is ill-formed

```cpp
String(String other);        // error
```

```
error: invalid constructor; you probably meant 'T (const T&)'
```

The compiler rejects it outright, and the reason is that it would be infinitely recursive: to
pass `other` **by value** you must copy it, which calls the copy constructor, which must pass
*its* parameter by value… The language forbids the declaration rather than letting you write
the infinite regress.

### It takes `const` — otherwise it binds almost nothing

`String(String&)` compiles, but from doc 03 §4 a non-const lvalue reference binds only to
non-const lvalues. So:

```cpp
String a;
String b = a;            // OK
const String c;
String d = c;            // ERROR: cannot bind const lvalue to String&
String e = makeString(); // ERROR: cannot bind rvalue to String&
```

Verified — both commented-out lines fail to compile. `const String&` binds all three.

### It copies the resource, not the pointer

This is the actual fix for doc 01. `new char[...]` gives this object its own buffer, so the two
destructors free two different blocks.

---

## 3. Copy assignment — the three jobs

```cpp
String& operator=(const String& other) {
    if (this == &other) return *this;         // 1. self-assignment guard

    delete[] m_Data;                          // 2. release what we already own

    m_Size = other.m_Size;                    // 3. acquire a copy of theirs
    m_Data = new char[m_Size + 1];
    std::memcpy(m_Data, other.m_Data, m_Size + 1);

    return *this;                             // 4. enable chaining
}
```

Miss **job 2** and you leak — the old buffer is orphaned. Miss **job 1** and you get §5. Miss
**job 4** and `a = b = c` does not compile.

### Why the return type is `T&` and not `void` or `T`

`T&` — a reference to the assigned-to object — matches the built-in types:

```cpp
int a, b, c;
a = b = c;              // works because (b = c) yields b itself
```

`operator=` returns `*this` so assignment chains right-to-left. Returning `T` by value would
work syntactically but would **copy on every assignment**, silently. Returning `void` breaks
chaining and, more importantly, breaks generic code and standard containers, which require
`T& operator=`.

> Returning `*this` also makes the result an **lvalue**, which is why `(a = b) = c` compiles.
> That is legal, useless, and a good consistency check on doc 03.

---

## 4. The complete correct `String`

```cpp
class String {
public:
    String() : m_Data(new char[1]{'\0'}), m_Size(0) {}

    String(const char* s)
        : m_Size(std::strlen(s)), m_Data(new char[m_Size + 1]) {
        std::memcpy(m_Data, s, m_Size + 1);
    }

    ~String() { delete[] m_Data; }

    String(const String& o)
        : m_Size(o.m_Size), m_Data(new char[o.m_Size + 1]) {
        std::memcpy(m_Data, o.m_Data, m_Size + 1);
    }

    String& operator=(const String& o) {
        if (this == &o) return *this;
        char* fresh = new char[o.m_Size + 1];          // acquire BEFORE releasing -- see below
        std::memcpy(fresh, o.m_Data, o.m_Size + 1);
        delete[] m_Data;
        m_Data = fresh;
        m_Size = o.m_Size;
        return *this;
    }

    const char* c_str() const { return m_Data; }
    std::size_t size()  const { return m_Size; }

private:
    std::size_t m_Size;
    char*       m_Data;
};
```

### Two details that are easy to get wrong

**Declaration order matters.** `m_Size` is declared before `m_Data`, and the initialiser list
uses `m_Size` to size `m_Data`. Members initialise in *declaration* order (doc 02 §2), so
`m_Size` is ready. Swap the declarations and `m_Data` is sized from an uninitialised
`m_Size` — reading garbage, allocating a random amount. The compiler warns with `-Wreorder`
only if the *initialiser list* order disagrees; it says nothing about this hazard.

**Allocate before deleting.** The version above builds `fresh` first, and only then releases
`m_Data`. If `new` throws, the object is untouched and still valid — the **strong exception
guarantee**, doc 08. Deleting first and then allocating leaves the object holding a dangling
pointer if the allocation fails. Same number of lines, strictly better behaviour.

---

## 5. Self-assignment — measured

`a = a` looks absurd, but it happens constantly through references and indices:

```cpp
v[i] = v[j];              // same element when i == j
*p = *q;                  // same object when p == q
node->left = node->left;  // after a rebalance
```

The naive implementation — release, then acquire:

```cpp
String& operator=(const String& o) {
    delete[] m_Data;                          // if &o == *this, we just freed o.m_Data
    m_Data = new char[strlen(o.m_Data) + 1];  // strlen on FREED memory
    strcpy(m_Data, o.m_Data);
    return *this;
}
```

Run it:

```
copy-and-swap, self-assignment:
  after c = c  -> "hello"  OK
copy-and-swap, normal assignment:
  a="bbbbb" b="bbbbb"  OK
naive, NORMAL assignment (works):
  a="bbbbb"  OK
naive, SELF assignment ->
  survived, data="@?M" (may be garbage)
done
---- exit: 0 ----
```

**It did not crash.** It exited 0 and the string is now `"@?M"`. The object is live, its
invariants look fine, `size()` would return a plausible number, and the data is wrong. This
class of bug propagates silently through a program until something far away misbehaves.

> GCC did emit `-Wrestrict` here — *"source argument is the same as destination"* — because it
> could see through the inlined `strcpy`. That is luck, not a safety net: it fires for `strcpy`
> specifically, not for the general pattern.

### Two fixes

**The guard.** `if (this == &other) return *this;` — one comparison, and it works. The
criticism is that you pay a branch on every assignment for a case that is rare, and that it is
easy to forget.

**Restructure so it cannot happen.** The version in §4 allocates the copy *first*, so even
without the guard, self-assignment copies from a buffer that is still alive. The guard becomes
an optimisation rather than a correctness requirement. **Prefer code that is correct without the
special case.**

The third answer — copy-and-swap — makes self-assignment structurally impossible *and* gives
you the strong exception guarantee for free. Doc 08 builds it.

---

## 6. `= delete` — when copying should not exist

For a type with unique ownership, deep copy is not merely expensive, it is *meaningless*. What
would a second `DiskManager` on the same file even be?

```cpp
DiskManager(const DiskManager&)            = delete;
DiskManager& operator=(const DiskManager&) = delete;
```

Three things this does:

- **Turns the double-free into a compile error**, at the line that tried to copy.
- **Documents the ownership model.** A reader sees "unique ownership" immediately.
- **Suppresses the implicit move operations too** (doc 06). If the type should be movable, you
  must then declare the moves explicitly — which is exactly what `PageGuard` does.

### Deleted vs private

The pre-C++11 trick was declaring copies `private` and never defining them. `= delete` is
better on three counts: the error is at overload resolution rather than access checking, so
even a `friend` cannot bypass it; the message says "use of deleted function" instead of a
confusing access error; and the intent is stated rather than encoded.

> `= delete` works on *any* function, not just special members. `void f(int) = delete;` next to
> `void f(long)` blocks a narrowing call. Useful, occasionally.

---

## 7. Which model needs which

| Ownership model | Copy ctor | Copy assign |
|---|---|---|
| Value / trivially copyable | implicit (say nothing) | implicit |
| Unique ownership | `= delete` | `= delete` |
| Deep / value-like | deep copy | deep copy |
| Shared | copy pointer, increment count | release old count, copy, increment |
| Non-owning view | implicit shallow copy is **correct** | implicit |

The last row is worth pausing on. `NodePage` holds a `Page&`. Copying it copies the reference —
two views onto one page, neither owning it. That is exactly right, and it is why `NodePage`
declares none of the five. Doc 11 §4 examines it.

---

## Checkpoint

- [ ] Implement `String` from §4 and verify with the `Tracer` pattern that copy construction
      and copy assignment fire in the situations you expect
- [ ] Write the naive `operator=` and reproduce the garbage output. **Do not skip this** — the
      fact that it survives is the lesson
- [ ] Deliberately swap the declaration order of `m_Size` and `m_Data` and observe the result.
      Note whether any warning fires
- [ ] Remove `return *this;` and find out which expression stops compiling
- [ ] Write `String(String&)` (non-const) and confirm the two failures from §2
- [ ] Answer: *why is `String(String other)` ill-formed rather than merely a bad idea?*
- [ ] Answer: *why does allocating before deleting give you the strong exception guarantee?*

Next: [05 — Move Semantics](05-move-semantics.md), where copying a 1 MB buffer becomes copying
one pointer.
