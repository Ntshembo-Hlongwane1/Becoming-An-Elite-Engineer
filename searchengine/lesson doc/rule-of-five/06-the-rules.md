# 06 — The Rules of 0, 3, and 5

> **This is the doc the series exists for.** Everything before it was building the vocabulary;
> everything after is consequences.
>
> The central fact, measured: **declaring a destructor — even an empty one, even
> `= default` — silently turns every move of your type into a copy.** No error, no warning, at
> any optimisation level. §2 has the proof.

---

## 1. The four states of a special member

Every one of the six special members is in exactly one of these states, and the vocabulary
matters because the rules are phrased in terms of it:

| State | How | Exists? |
|---|---|---|
| **User-provided** | you wrote a body: `~X(){}` | yes |
| **User-declared, defaulted** | `~X() = default;` | yes |
| **User-declared, deleted** | `~X() = delete;` | no — using it is an error |
| **Implicitly declared** | you said nothing | maybe — the compiler decides |

The critical subtlety, and the one people get wrong:

> **"User-declared" includes `= default` and `= delete`.** Writing `~X() = default;` is *not*
> the same as writing nothing.

Measured:

```
does '= default' count as user-declared?
  struct A { P p; };                     member was MOVED
  struct B { ... ~B(){}  };              member was COPIED
  struct C { ... ~C() = default; };      member was COPIED     <-- !
  struct D { P p; };  (control)          member was MOVED
```

`~C() = default;` looks like a documentation gesture — "I'm being explicit that the default is
fine." It costs you every move in your program.

---

## 2. The suppression matrix — measured, cell by cell

Each row is a class that declares exactly the listed member and nothing else. The columns are
what the compiler then provides.

```
user-DECLARED members              | copy operations                | move operations
-----------------------------------+--------------------------------+---------------------------
(nothing)  -- Rule of Zero         | copy-ctor:ok       copy-asg:ok  | move-ctor:real move  move-asg:real move
~X() only                          | copy-ctor:ok       copy-asg:ok  | move-ctor:falls to COPY  move-asg:falls to COPY
X(const X&) only                   | copy-ctor:ok       copy-asg:ok  | move-ctor:falls to COPY  move-asg:falls to COPY
operator=(const X&) only           | copy-ctor:ok       copy-asg:ok  | move-ctor:falls to COPY  move-asg:falls to COPY
X(X&&) only                        | copy-ctor:DELETED  copy-asg:DELETED | move-ctor:real move  move-asg:DELETED
operator=(X&&) only                | copy-ctor:DELETED  copy-asg:DELETED | move-ctor:DELETED    move-asg:real move
all five (Rule of Five)            | copy-ctor:ok       copy-asg:ok  | move-ctor:real move  move-asg:real move
```

Condensed into the two rules that generate every row:

> **Rule S (suppression).** Declaring **any** of {destructor, copy ctor, copy assign, move ctor,
> move assign} prevents the compiler from implicitly generating the **move** operations.
>
> **Rule D (deletion).** Declaring **either** move operation **deletes** both copy operations.

### The two failure modes are not equally dangerous

This asymmetry is the practical heart of the matter:

| | Mechanism | How you find out |
|---|---|---|
| Rule S | Move is never generated; overload resolution falls back to the copy, which accepts rvalues (doc 03 §4) | **Silently. It just runs slower.** |
| Rule D | Copy is `delete`d | **Loudly.** Compile error at the call site |

Rule D is a good citizen: you asked for move-only semantics and got them, and misuse is a build
failure.

Rule S is the trap. A type with a destructor and a `std::vector<char>` member looks movable, is
used as though it is movable, and copies its buffer every single time. On a `vector` of those
types, every reallocation deep-copies every element. **The program is correct and slow, and the
cause is a line you added for an unrelated reason.**

### Why the language does this

It is a safety rule, and it is defensible. If you wrote a destructor, you are managing something
by hand. A compiler-generated move — which memberwise-moves and leaves the source in some state
the compiler chose — might well be wrong for whatever you are managing. So rather than guess,
the language declines to generate one, and falls back to the copy, which you either wrote or
accepted.

Conservative and safe. Also invisible, which is why you have to know about it.

> Rule S technically also applies to the *copy* operations (declaring a destructor makes the
> implicit copies **deprecated**), but they are still generated for backwards compatibility with
> pre-C++11 code. Deprecated for over a decade and still generated — so do not expect a warning.

---

## 3. Rule of Zero — the one to aim for

> **If your class does not directly manage a resource, declare none of the six.**

```cpp
struct Config {
    std::string        name;
    std::vector<int>   values;
    std::unique_ptr<T> owned;
};
```

Zero special members. And it is fully correct: copyable if all members are, movable if all
members are, destructible, exception-safe. Every member manages itself, so the compiler's
memberwise versions are exactly right.

**This should be the shape of most of your classes.** The special members are for the small
number of types that sit directly on a raw resource — and those types should be small, single-
purpose wrappers that everything else holds as members.

That is precisely the design in your storage engine: `PageGuard` manages the pin and nothing
else; anything holding a `PageGuard` needs no special members of its own.

### The Rule of Zero has one enemy: the "harmless" destructor

```cpp
struct Session {
    std::vector<char> buffer;
    ~Session() { log("session closed"); }      // <-- just logging!
};
```

That destructor manages no resource. It also, by Rule S, disables both move operations, so every
`Session` move deep-copies `buffer`. If you need the logging, you must now write the other four
members to get back what you had for free.

**A destructor that does not release a resource is nearly always a design smell.** Logging
belongs in a dedicated RAII logger member — which restores the Rule of Zero and keeps the
behaviour.

---

## 4. Rule of Three — the historical form

> **If you declare any of {destructor, copy constructor, copy assignment}, you almost certainly
> need all three.**

Pre-C++11, these were the only three. The logic is doc 01: needing a destructor means you own
something, and owning something means the compiler's memberwise copy is wrong.

Still correct today, and still the fastest test to apply: **if you wrote `delete`, `free`,
`fclose`, or `Unpin` in a destructor, you owe the other two members an answer** — either an
implementation or `= delete`.

---

## 5. Rule of Five — the modern form

> **If you declare any one of the five, declare all five.**

Not because you always need five implementations — but because declaring one changes what the
compiler does with the others, and by declaring all five you take that decision away from a
rule most readers do not have memorised.

The three shapes you will actually write:

### Shape A — unique ownership (move-only)

```cpp
~PageGuard();
PageGuard(const PageGuard&)            = delete;
PageGuard& operator=(const PageGuard&) = delete;
PageGuard(PageGuard&&) noexcept;
PageGuard& operator=(PageGuard&&) noexcept;
```

The resource cannot be duplicated. `PageGuard`, `unique_ptr`, `DiskManager`, `BufferPool`,
`std::thread`, `std::fstream`.

### Shape B — value-like (deep copy + move)

```cpp
~String();
String(const String&);                 // deep copy
String& operator=(const String&);
String(String&&) noexcept;             // steal
String& operator=(String&&) noexcept;
```

The resource can be duplicated and copying is meaningful. `std::string`, `std::vector`.

### Shape C — non-copyable, non-movable

```cpp
~Mutex();
Mutex(const Mutex&)            = delete;
Mutex& operator=(const Mutex&) = delete;
Mutex(Mutex&&)                 = delete;
Mutex& operator=(Mutex&&)      = delete;
```

The object's *address* is part of its identity, so it must not relocate. `std::mutex`,
`std::atomic`, and anything registered in a table by pointer.

### Restoring what a destructor took away

If you need a destructor but want the compiler's memberwise moves:

```cpp
struct Session {
    std::vector<char> buffer;
    ~Session() { log("closed"); }

    Session(Session&&) noexcept            = default;   // explicitly restore
    Session& operator=(Session&&) noexcept = default;
    Session(const Session&)                = default;
    Session& operator=(const Session&)     = default;
};
```

Verified — this is the `D` row from doc 00's opening experiment:

```
D  (destructor + explicitly defaulted moves):
      member MOVED
```

`= default` on the *moves* generates real moves. `= default` on the *destructor* is what
suppressed them. Same keyword, opposite effect, depending on which member it is attached to.

---

## 6. The decision procedure

Ask one question, then follow the row.

> **Does this class directly manage a resource that must be released?**

| Answer | Declare |
|---|---|
| **No** | **nothing.** Rule of Zero |
| Yes, and copying is meaningless | Shape A — dtor, delete copies, implement moves |
| Yes, and copying is meaningful | Shape B — all five implemented |
| Yes, and the object must not move | Shape C — dtor, delete all four |
| No, but I need a destructor for a side effect | reconsider; if unavoidable, Shape B with all four `= default` |

"Directly" is the load-bearing word. A class holding a `unique_ptr` does not *directly* manage
memory — the `unique_ptr` does. Rule of Zero applies.

---

## 7. Auditing your own code

Apply the matrix to what you have written.

**`NodePage`** (`storage/05`) — holds a `Page&`, owns nothing. Declares nothing. **Rule of Zero,
correct.** Note that the reference member makes copy-assignment implicitly deleted anyway
(doc 04 §7), which is fine: nobody needs to reassign a view.

**`DiskManager`** (`storage/03`) — owns a `FILE*`. Declares destructor + deleted copies. By
Rule S the moves are suppressed, so `DiskManager` is **immovable**. That happens to be what we
want (it lives for the program's duration), but it is currently accidental rather than stated.
Doc 11 §2 makes it explicit.

**`BufferPool`** (`storage/06`) — owns `unique_ptr<Frame[]>` plus a destructor calling
`FlushAll()`. Same situation, same conclusion.

**`PageGuard`** (`storage/08`) — all five declared. Shape A. **Correct by construction**, and
now you know why each of the five is there rather than just that they are.

**Your in-memory `BPlusTree`** — owns raw node pointers, has a destructor, deletes copies,
implements moves. Shape A, correct. Doc 11 §5 examines the move implementation in detail.

---

## Checkpoint

- [ ] Reproduce the suppression matrix. **Predict each row before running it**
- [ ] Reproduce the `= default` destructor result — it is the most surprising cell
- [ ] Take a class with a `std::vector` member, add `~X() = default;`, and measure the
      difference in a `vector<X>` reallocation (doc 07's harness does this)
- [ ] Write a class declaring only `X(X&&)` and confirm that `X b = a;` fails to compile. Read
      the exact error text
- [ ] Audit every class in `internal/kernal/` against §6's table. Write down which shape each is
- [ ] Answer: *why is Rule S silent and Rule D loud, and which is more dangerous?*
- [ ] Answer: *why does declaring a destructor suppress moves? Is the language being unhelpful,
      or careful?*

Next: [07 — `noexcept` and Why Containers Care](07-noexcept.md), where one keyword is worth
fifteen deep copies.
