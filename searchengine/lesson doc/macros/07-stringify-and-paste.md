# 07 — Stringify `#` and Token-Paste `##`

> Two operators that exist only inside macros, and that let the preprocessor do something no
> other part of C++ can: **manipulate the source text itself.**
>
> This is the mechanism behind `assert` printing the expression you wrote, and behind X-macros —
> the one code-generation technique worth knowing.

---

## 1. `#` — stringify

Turns a macro parameter into a string literal containing its **source text**.

```cpp
#define STR_BAD(x) #x
const char* a = STR_BAD(VERSION);
```

Expands to:

```cpp
const char* a = "VERSION";
```

It captured the *token*, not the value. That is usually not what you want — but it is exactly
what `assert` wants:

```cpp
#define ASSERT(e) ((e) ? (void)0 : Fail(#e, __FILE__, __LINE__))

ASSERT(index < keys.size());
// -> Fail("index < keys.size()", "NodePage.hpp", 142)
```

**No function can do this.** By the time a function receives an argument, it has a value; the
text is gone. The combination of `#e`, `__FILE__`, and `__LINE__` is why `assert` is and must be
a macro.

Stringification also normalises whitespace (runs collapse to one space) and escapes `"` and `\`
correctly, so `ASSERT(s == "hi")` produces a valid literal.

---

## 2. The two-level trick

The problem: `#` stringifies **before** the argument is macro-expanded.

```cpp
#define VERSION 3
#define STR_BAD(x)  #x
#define STR(x)      STR_BAD(x)      // <- extra level

const char* a = STR_BAD(VERSION);
const char* b = STR(VERSION);
```

Verified expansion:

```cpp
const char* a = "VERSION";      // stringified the NAME
const char* b = "3";            // expanded first, THEN stringified
```

**Why the extra level works:** when `STR(VERSION)` is expanded, its argument `VERSION` is
macro-expanded to `3` *before* being substituted into `STR`'s body — because `STR`'s body does
not apply `#` to it directly. `STR_BAD(3)` then stringifies `3`.

The rule: **an argument is macro-expanded before substitution, unless it is the operand of `#`
or `##`.** So you need one indirection to force expansion first.

This is why you constantly see paired macros in real code:

```cpp
#define SEDB_STRINGIFY_(x) #x
#define SEDB_STRINGIFY(x)  SEDB_STRINGIFY_(x)

#define SEDB_VERSION_MAJOR 1
#define SEDB_VERSION_MINOR 4
const char* version = SEDB_STRINGIFY(SEDB_VERSION_MAJOR) "."
                      SEDB_STRINGIFY(SEDB_VERSION_MINOR);   // "1" "." "4" -> "1.4"
```

(Adjacent string literals concatenate at compile time — a C++ feature, not a macro one.)

---

## 3. `##` — token paste

Joins two tokens into one.

```cpp
#define CAT(a,b) a##b
int CAT(my,Var) = 7;
```

Expands to:

```cpp
int myVar = 7;
```

Note the result must be a **valid single token**. `CAT(1,+)` produces `1+`, which is not one
token, and is undefined behaviour (GCC reports "pasting does not give a valid preprocessing
token").

`##` obeys the same non-expansion rule as `#`, so it needs the same two-level dance:

```cpp
#define CAT_(a,b) a##b
#define CAT(a,b)  CAT_(a,b)

#define PREFIX my
int CAT_(PREFIX, Var) = 1;    // PREFIXVar   <- not expanded
int CAT (PREFIX, Var) = 2;    // myVar       <- expanded
```

### Where paste is genuinely used

**Unique names** — for RAII helpers that need a variable name nobody typed:

```cpp
#define SEDB_CONCAT_(a,b) a##b
#define SEDB_CONCAT(a,b)  SEDB_CONCAT_(a,b)
#define SEDB_UNIQUE(base) SEDB_CONCAT(base, __LINE__)

#define SCOPED_TIMER(name) Timer SEDB_UNIQUE(timer_)(name)

SCOPED_TIMER("descent");     // -> Timer timer_42("descent");
SCOPED_TIMER("split");       // -> Timer timer_43("split");
```

Without the paste, the second use would redeclare the same name. This is exactly how
`std::lock_guard` helper macros and most test frameworks generate their hidden variables.
(`__COUNTER__`, a non-standard but universal extension, is better than `__LINE__` when two
appear on one line.)

**Generated accessors** — mostly a smell, occasionally justified:

```cpp
#define FIELD(type, name)                    \
    type m_##name;                           \
    type Get##name() const { return m_##name; }   \
    void Set##name(type v) { m_##name = v; }
```

Concise, and it defeats grep: searching for `GetKeyCount` finds nothing, because that identifier
appears nowhere in the source. **That cost is usually decisive** — code you cannot search is code
you cannot maintain.

---

## 4. X-macros — the code generation pattern worth knowing

The one legitimate large-scale use, and the answer to *"I have a list of things and three places
that must stay in sync."*

```cpp
#define NODE_TYPE_LIST          \
    X(Internal, 0, "internal")  \
    X(Leaf,     1, "leaf")      \
    X(Overflow, 2, "overflow")

enum class NodeType : unsigned {
#define X(name, val, str) name = val,
    NODE_TYPE_LIST
#undef X
};

const char* ToString(NodeType t){
    switch(t){
#define X(name, val, str) case NodeType::name: return str;
    NODE_TYPE_LIST
#undef X
    }
    return "?";
}
```

Verified expansion and output:

```cpp
enum class NodeType : unsigned {
    Internal = 0, Leaf = 1, Overflow = 2,
};
```
```
  0 -> internal
  1 -> leaf
  2 -> overflow
```

**The idea:** the list is written once. Each consumer defines `X` to mean whatever it needs,
includes the list, and `#undef`s `X`. Add `Overflow` in one place and the enum, the `ToString`,
the parser, and the validator all update together.

**Why it beats the alternatives:** the enum and its `ToString` cannot drift out of sync, because
they are generated from one source. That class of bug — add an enumerator, forget the switch — is
extremely common and completely eliminated here. (A `switch` over a scoped enum with
`-Wswitch` catches the missing case, which is the non-macro half of the defence; X-macros
catch it even where there is no switch.)

**The costs**, and they are real:

- The generated code is invisible; you must run `-E` to review it.
- Error messages point into the list.
- Debuggers and IDEs cannot navigate it.
- `#undef X` is mandatory, and forgetting it breaks the next use bizarrely.

**Use it when the list has three or more consumers and drift is a genuine risk.** For two
consumers, write both by hand. C++ has no better answer here — reflection, arriving in C++26,
is the eventual replacement.

---

## 5. Precedence and the surprises

`#` and `##` are evaluated left to right, after argument substitution and before rescanning. Two
consequences people trip on:

```cpp
#define BAD(x) ##x           // ERROR: ## must be BETWEEN two tokens
#define HASH_HASH # ## #     // legal, produces the token ##. Do not do this.
```

And the empty-argument case:

```cpp
#define JOIN(a,b) a##b
JOIN(x,)      // -> x     (pasting with nothing is allowed and yields the other token)
JOIN(,)       // -> nothing
```

This is the mechanism behind the old GNU `, ## __VA_ARGS__` comma-swallowing hack that doc 08
replaces with `__VA_OPT__`.

---

## Checkpoint

- [ ] Reproduce the `STR_BAD` vs `STR` difference and explain the extra level
- [ ] Write `SEDB_STRINGIFY` and use it to build a version string from two numeric macros
- [ ] Write `SEDB_UNIQUE` and use it twice in one function; confirm with `-E` that the names differ
- [ ] Try `CAT(1,+)` and read the error
- [ ] Build the X-macro example, add a fourth type, and confirm both the enum and `ToString`
      update from the single edit
- [ ] Remove one `#undef X` and observe how the next use fails
- [ ] Answer: *why does `#` need a second macro level to stringify a value rather than a name?*
- [ ] Answer: *why must `assert` be a macro?*

Next: [08 — Variadic Macros](08-variadic-macros.md).
