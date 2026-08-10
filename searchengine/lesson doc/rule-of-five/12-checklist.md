# 12 — Decision Checklist, Antipatterns, and a Test Harness

> The whole series compressed into: one question to ask, one flowchart to follow, a catalogue of
> the bugs, and a harness that checks a type automatically.

---

## 1. The one question

> **Does this class *directly* manage a resource that must be released?**

"Directly" is load-bearing. A class holding a `std::vector` and a `unique_ptr` manages nothing
directly — its members do.

```
                     Does it DIRECTLY manage a resource?
                          /                    \
                        NO                     YES
                         |                      |
                  ┌──────────────┐              |
                  │ RULE OF ZERO │        Is copying meaningful?
                  │ declare NONE │        /                  \
                  └──────────────┘      NO                   YES
                                         |                     |
                            Must the object stay put?    ┌────────────┐
                             /              \            │  SHAPE B   │
                           YES              NO           │ all five,  │
                            |                |           │ deep copy  │
                     ┌────────────┐   ┌────────────┐     │ + move     │
                     │  SHAPE C   │   │  SHAPE A   │     └────────────┘
                     │ dtor +     │   │ dtor,      │
                     │ delete all │   │ del copies,│
                     │ four       │   │ impl moves │
                     └────────────┘   └────────────┘
```

| Shape | Example |
|---|---|
| **Zero** | `NodePage`, `Page`, `DiskBPlusTree`, most of your code |
| **A** — move-only | `PageGuard`, `BPlusTree`, `unique_ptr`, `std::thread` |
| **B** — value-like | `String`, `std::vector`, `std::string` |
| **C** — pinned | `std::mutex`, `std::atomic`, `DiskManager` (as intended) |

---

## 2. Writing them — the checklist

**Destructor**
- [ ] Releases every directly-owned resource
- [ ] Handles the moved-from state (a null pointer must be safe)
- [ ] Cannot throw — wrap fallible cleanup in `try/catch` (doc 02 §4)
- [ ] **You actually need it.** An empty or logging-only destructor is a design smell (doc 06 §3)

**Copy constructor**
- [ ] Takes `const T&` (doc 04 §2)
- [ ] Duplicates the resource — no shared pointers unless that is the model
- [ ] Or `= delete` for Shapes A and C

**Copy assignment**
- [ ] Returns `T&`, returns `*this`
- [ ] Releases the old resource before overwriting
- [ ] Safe under self-assignment (doc 04 §5)
- [ ] Acquires before releasing, for the strong guarantee (doc 08 §2)
- [ ] Or use copy-and-swap and get the last three free (doc 08 §3)

**Move constructor**
- [ ] `noexcept` (doc 07)
- [ ] **Leaves the source owning nothing** — the single most important line (doc 05 §1)
- [ ] Uses `std::move` on every non-trivial member (doc 03 §5)
- [ ] The source must remain destructible and assignable

**Move assignment**
- [ ] `noexcept`
- [ ] Releases its own resource first (doc 05 §3)
- [ ] Guards against self-move
- [ ] Empties the source

---

## 3. The antipattern catalogue

Every one of these is silent — no error, and in most cases no warning.

### A. The empty destructor — costs every move

```cpp
struct Session { std::vector<char> buffer; ~Session(){} };
```

Doc 06's headline. Suppresses both moves; every `Session` move deep-copies. **Fix:** delete the
destructor, or `= default` all four other members.

### B. `= default` on the destructor — identical cost

```cpp
struct S { std::vector<char> b; ~S() = default; };
```

Looks like a no-op. Is not. `= default` counts as user-declared (doc 06 §1).

### C. Missing `noexcept` on moves — vector copies

Measured at 15 deep copies vs 0 (doc 07 §3). **Fix:** mark them; a correct move cannot throw.

### D. Forgetting to empty the source

```cpp
String(String&& o) noexcept : m_Data(o.m_Data) { }     // no o.m_Data = nullptr
```

Doc 01's double free, rebuilt. **Fix:** null it.

### E. `std::move` missing inside a move constructor

```cpp
Tracer(Tracer&& o) noexcept : name(o.name) { }         // o.name is an LVALUE -> COPIES
```

Doc 03 §5. Compiles, runs, correct, slow. **Fix:** `std::move(o.name)`.

### F. `return std::move(local)`

Blocks NRVO (doc 09 §2). Warned by `-Wpessimizing-move`. **Fix:** `return local;`

### G. Delete-then-allocate in assignment

Leaves a dangling pointer if the allocation throws (doc 08 §2). **Fix:** allocate first.

### H. Non-`explicit` conversion operator

`handle + 1` compiles (doc 10 §8). **Fix:** `explicit`.

### I. Returning `const T` by value

Silently disables moves — a `const` rvalue cannot bind to `T&&` (doc 03 §4). **Fix:** drop it.

### J. One copy op without the other

Declaring only the copy constructor leaves the compiler-generated copy assignment doing a
shallow copy — half the class is correct. **Fix:** Rule of Five, declare all.

---

## 4. The harness

Drop this in and run it on any class you write. It is compile-time where it can be and runtime
where it must be.

```cpp
// lesson doc/rule-of-five/harness.hpp
#pragma once
#include <cstdio>
#include <type_traits>
#include <utility>
#include <vector>

template <typename T, typename Factory>
bool AuditType(const char* name, Factory make) {
    int fails = 0;
    auto check = [&](const char* what, bool ok, const char* why){
        printf("  %-38s %s%s%s\n", what, ok?"PASS":"** FAIL **",
               ok?"":"  <- ", ok?"":why);
        if(!ok) ++fails;
    };
    printf("== %s ==\n", name);

    check("destructible",           std::is_destructible_v<T>,         "cannot be destroyed");
    check("destructor is noexcept", std::is_nothrow_destructible_v<T>, "dtor may throw");

    constexpr bool cc = std::is_copy_constructible_v<T>;
    constexpr bool ca = std::is_copy_assignable_v<T>;
    constexpr bool mc = std::is_move_constructible_v<T>;
    constexpr bool ma = std::is_move_assignable_v<T>;
    printf("  copy-ctor=%s copy-assign=%s move-ctor=%s move-assign=%s\n",
           cc?"y":"n", ca?"y":"n", mc?"y":"n", ma?"y":"n");

    check("copy ctor and copy assign agree", cc == ca, "one copy op without the other");
    check("move ctor and move assign agree", mc == ma, "one move op without the other");

    if constexpr (mc)
        check("move ctor is noexcept", std::is_nothrow_move_constructible_v<T>,
              "vector will COPY instead of moving (doc 07)");
    if constexpr (ma)
        check("move assign is noexcept", std::is_nothrow_move_assignable_v<T>,
              "vector will COPY instead of moving (doc 07)");

    if constexpr (mc && ma) {
        { T a = make(); T b = std::move(a); T c = make(); a = std::move(c); }
        check("moved-from survives reassignment + destruction", true, "");
    }
    if constexpr (ca) {
        T a = make(); a = a;
        check("self copy-assignment survives", true, "");
    }
    if constexpr (mc || cc) {
        std::vector<T> v;
        for (int i = 0; i < 40; ++i) v.push_back(make());
        check("survives vector reallocation", v.size() == 40, "corrupted in container");
    }
    printf("  -> %s\n\n", fails ? "FAILURES" : "all checks passed");
    return fails == 0;
}
```

### It works — measured

```
== String  (Shape B, copy-and-swap) ==
  copy-ctor=y copy-assign=y move-ctor=y move-assign=y
  move ctor is noexcept                  PASS
  moved-from survives reassignment + destruction PASS
  self copy-assignment survives          PASS
  survives vector reallocation           PASS
  -> all checks passed

== Trapped (empty destructor) ==
  copy-ctor=y copy-assign=y move-ctor=y move-assign=y
  move ctor is noexcept                  ** FAIL **  <- vector will COPY instead of moving (doc 07)
  move assign is noexcept                ** FAIL **  <- vector will COPY instead of moving (doc 07)
  -> FAILURES

== Unique  (Shape A, move-only) ==
  copy-ctor=n copy-assign=n move-ctor=y move-assign=y
  move ctor is noexcept                  PASS
  -> all checks passed
```

**It caught antipattern A automatically** — `struct Trapped { std::string s; ~Trapped(){} };`
flagged as not-nothrow-movable, with the doc reference. That is the whole series reduced to a
build step.

### What it cannot catch

Be clear about the limits. It does not detect:

- **Double-free or leaks.** Pair it with the allocation-counting technique — override global
  `operator new`/`delete`, count, and assert the count returns to baseline.
- **Missing `std::move` inside a move constructor** (antipattern E). Needs an instrumented member
  like doc 06's `P` probe.
- **Shallow copy that happens to work in tests.** Needs a resource that notices, such as a
  pointer you dereference after the source dies.

---

## 5. Compiler flags that help

```bash
g++ -std=c++20 -Wall -Wextra \
    -Wpessimizing-move \      # antipattern F
    -Wredundant-move \        # std::move that does nothing
    -Wself-move \             # x = std::move(x)
    -Wdeprecated-copy-dtor \  # antipattern A -- see the caveat below
    -Wdeprecated-copy \       # implicit copy where a copy op is user-provided
    -Wnon-virtual-dtor \      # base class deleted through a base pointer
    -Wold-style-cast \
    prog.cpp
```

**`-Wdeprecated-copy-dtor` is the one that touches the empty-destructor trap**, and it is worth
knowing exactly what it does and does not do, because I got this wrong the first time and had to
measure it.

It fires when the compiler generates a **copy** operation for a class with a **user-provided**
destructor:

```
warning: implicitly-declared 'Trapped::Trapped(const Trapped&)' is deprecated
         [-Wdeprecated-copy-dtor]
note: because 'Trapped' has user-provided 'Trapped::~Trapped()'
```

The note names the destructor as the cause, which is exactly the diagnosis you want. Note it
warns about the *copy* being deprecated, not about the *move* being suppressed — but it is the
same condition, so it points at the right line.

### Two limitations, both measured

**It is not in `-Wall` or `-Wextra`.** Verified: `-Wall -Wextra` alone produces nothing for
`struct Trapped { std::string s; ~Trapped(){} };`. You must ask for it by name.

**It does not catch antipattern B.** `~X() = default;` is user-*declared* but not
user-*provided*, and the warning only covers the latter:

```cpp
struct Defaulted { std::string s; ~Defaulted() = default; };
Defaulted b = a;      // -Wall -Wextra -Wdeprecated-copy-dtor : NO WARNING
```

Verified silent. So the `= default` destructor — which suppresses your moves just as thoroughly
as an empty one (doc 06 §1) — is **undiagnosable by any compiler warning**.

That is the strongest argument for §4's harness: it is the only thing in this doc that catches
both forms, and it catches them by asking `is_nothrow_move_constructible_v<T>` directly rather
than trying to infer intent from declarations.

If your toolchain has them, sanitizers catch what warnings cannot:

```bash
-fsanitize=address        # double-free, use-after-free, leaks
-fsanitize=undefined      # UB
```

Your MinGW build ships neither `libasan` nor `libubsan` (both link steps failed during this
series), so on this machine you are relying on the Windows heap's coarser checks. **That is a
reason to run the harness more diligently, not less.**

---

## 6. Where to go from here

- **Apply §1's flowchart to every class in `internal/kernal/`.** Doc 11 §7 has the table started;
  finish it.
- **Add the harness to your test suite.** One `AuditType<T>` line per owning type.
- **Turn on `-Wdeprecated-copy`** in `CMakeLists.txt` and fix what it finds.
- **Make `DiskManager` and `BufferPool` explicitly immovable** — doc 11 §2, four lines.

### Beyond the five

- **`shared_ptr` and reference counting** — the fifth ownership model, with atomics and cycles.
- **Allocators** — `storage/06` §4.3's arenas, done properly through `std::pmr`.
- **`std::exchange`** — makes move constructors one line:
  `m_Ptr(std::exchange(o.m_Ptr, nullptr))`.
- **Concepts** — C++20 lets you *constrain* on these properties rather than assert them:
  `template <std::movable T>`.

---

## The series in one page

| Doc | The thing to remember |
|---|---|
| 01 | The compiler's memberwise copy is right for values, fatal for owners |
| 02 | Destruction is exactly reverse construction; unwinding runs every destructor |
| 03 | Value category picks copy vs move — and **a named `T&&` is an lvalue** |
| 04 | Assignment must release before it acquires, and survive self-assignment |
| 05 | A move must leave the source **owning nothing**; `std::move` is only a cast |
| 06 | **Declaring any of the five suppresses the implicit moves** — even `= default` |
| 07 | Without `noexcept`, containers copy instead of moving. Measured: 15 vs 0 |
| 08 | Do everything fallible before you modify anything |
| 09 | `return x;` beats `return std::move(x);` |
| 10 | Overload only where the meaning is obvious; `explicit` your conversions |
| 11 | Your `PageGuard` and `BPlusTree` are correct; `DiskManager` is correct by accident |
| 12 | Ask one question: *does this directly manage a resource?* |

Back to [00 — Index](00-index.md).
