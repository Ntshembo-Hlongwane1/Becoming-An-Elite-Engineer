# 02 — Object-like Macros

> `#define PAGE_SIZE 4096`. The simplest macro, the most common, and — for constants — almost
> always the wrong tool since 1998.
>
> This doc is the argument for `inline constexpr`, which `lesson doc/storage/02-the-page.md` §1
> used without fully justifying against the alternative.

---

## 1. What it is

```cpp
#define MAX_SIZE 100
```

*"From here to the end of the translation unit, replace the token `MAX_SIZE` with the token
`100`."* That is the entire semantics. Not a variable, not a constant, not typed, not scoped —
a find-and-replace rule over tokens.

Note it is **tokens**, not characters. `MAX_SIZEX` is not replaced, because that is a different
single token. But the replacement list is otherwise arbitrary text:

```cpp
#define BEGIN {
#define END   }
void f() BEGIN END       // legal, and an offence against your colleagues
```

---

## 2. The five problems

### 2.1 No scope

Macros ignore namespaces, classes, functions, and blocks. There is exactly one flat namespace of
macro names, and it spans everything textually after the `#define`.

```cpp
#define MAX_SIZE 100
struct Widget { int MAX_SIZE; };
```

```
error: expected unqualified-id before numeric constant
    1 | #define MAX_SIZE 100
      |                  ^~~
note: in expansion of macro 'MAX_SIZE'
```

The member declaration became `int 100;`. Notice where the error points — at the **`#define`
line**, not at the struct. This is the signature confusion of macro errors and doc 10 is about
decoding it.

The practical consequence: **a macro in a header poisons every file that includes it,
transitively.** This is why `<windows.h>` is notorious — it defines `min`, `max`, `near`, `far`,
`small`, and hundreds more, and including it breaks `std::min` and any variable named `small`
anywhere in your program. The standard workaround is a macro (`#define NOMINMAX`), which tells
you how deep the hole goes.

### 2.2 No type

```cpp
#define MAX_SIZE 100                       // int? unsigned? long?
inline constexpr std::size_t MaxSize = 100; // std::size_t. Stated.
```

The macro's type is whatever the literal's type happens to be — `int` here. Feed it to something
expecting `std::size_t` and you get an implicit conversion the compiler may warn about, and the
integer-promotion hazards of `storage/03` §4.

This is exactly why `PAGE_SIZE` is `inline constexpr std::size_t` rather than `#define
PAGE_SIZE 4096`. With the macro, `pageId * PAGE_SIZE` would be `uint32_t * int` — **32-bit
arithmetic, overflowing at 4 GB.** With the typed constant it is `uint32_t * size_t`, 64-bit.
The type is not decoration; it changes the arithmetic.

### 2.3 Invisible to the debugger and the compiler's diagnostics

The macro name does not exist after phase 4. You cannot inspect `MAX_SIZE` in a debugger, hover
it in an IDE for a value, or see it in a symbol table. `constexpr` constants have debug info.

### 2.4 No address, no reference

```cpp
const std::size_t* p = &MAX_SIZE;        // &100 -- error
void f(const std::size_t&);
f(MAX_SIZE);                             // binds to a temporary, not to a constant
```

A macro is a value, never an object. Doc `storage/02` §1's `inline constexpr` discussion is the
other half of this: `constexpr` gives you an object *when one is needed*, and `inline` ensures
there is exactly one of them.

### 2.5 Expansion happens at use, not at definition

```cpp
#define AREA WIDTH * HEIGHT      // WIDTH and HEIGHT need not exist yet
#define WIDTH 10
#define HEIGHT 20
int a = AREA;                    // resolves here: 10 * 20
```

Legal, and it means a macro's meaning depends on what is defined at every *use site*. Redefine
`WIDTH` halfway down the file and `AREA` silently means something different below that point.

---

## 3. The replacement

```cpp
// instead of:
#define MAX_SIZE 100
#define PI 3.14159
#define APP_NAME "searchengine"

// write:
inline constexpr std::size_t MaxSize = 100;
inline constexpr double      Pi      = 3.14159;
inline constexpr const char* AppName = "searchengine";
```

Every problem in §2 disappears: they are scoped, typed, debuggable, addressable, and resolved at
definition. And they cost nothing — a `constexpr` constant used as a value is substituted just
like a macro, with no storage unless you take its address.

`inline` is what makes it safe in a header. Without it, each translation unit gets its own
object with its own address; `storage/02` §1 measured that and it is the reason for the keyword.

### For sets of related constants

```cpp
// instead of:
#define NODE_INTERNAL 0
#define NODE_LEAF     1

// write:
enum class NodeType : std::uint16_t { Internal = 0, Leaf = 1 };
```

Now they are one type, they do not implicitly convert to `int`, their width is fixed for the
page format, and `switch` can warn about unhandled cases. This is exactly the choice `NodePage`
makes (`storage/05` §6).

---

## 4. The comparison table

| | `#define` | `inline constexpr` |
|---|---|---|
| Scoped to namespace/class | **no** | yes |
| Has a type | **no** | yes |
| Visible to debugger | **no** | yes |
| Can take its address | **no** | yes |
| Usable in constant expressions | yes | yes |
| Zero runtime cost | yes | yes |
| Can be `#undef`'d / redefined mid-file | yes | no *(a feature)* |
| Usable in `#if` | **yes** | **no** |

The last row is the only one favouring the macro, and it is the one real reason a constant might
have to be a macro: **`#if` cannot see C++ entities.**

```cpp
#define ENABLE_STATS 1
#if ENABLE_STATS          // works
...
inline constexpr bool EnableStats = true;
#if EnableStats           // does NOT work -- expands to "#if EnableStats" -> 0
```

An undefined identifier in `#if` evaluates to `0`, silently. So the branch is quietly taken and
nothing warns. If a value must drive conditional compilation, it must be a macro. If it must
also be usable in C++, define both — doc 11 §5 shows the config-header pattern that keeps them
in sync.

---

## 5. Naming conventions, and why they matter more here

Because macros have no scope, the name is the *only* collision protection you have.

- **`SCREAMING_SNAKE_CASE`, always.** Universal convention: an all-caps identifier warns the
  reader "this may not obey normal rules."
- **Never name a non-macro in `SCREAMING_SNAKE_CASE`.** The convention only works if it is
  one-to-one.
- **Prefix project macros** — `SEDB_PAGE_SIZE`, not `PAGE_SIZE`. The `PORTABLE_FSEEK` in
  `storage/03` follows this.
- **Names beginning with `_` or containing `__` are reserved to the implementation.** Never
  define them. `_WIN32`, `__GNUC__`, `__LINE__` are the compiler's; competing is undefined
  behaviour, and doc 05 §2 revisits it for include guards.

---

## 6. Undefining and redefining

```cpp
#define X 1
#define X 2          // warning: "X" redefined
#undef X
#define X 2          // fine
```

Redefining without `#undef` is a warning if the definitions differ, silent if identical. `#undef`
on a name that was never defined is legal and does nothing.

This is occasionally the only way out of a header conflict:

```cpp
#include <windows.h>
#undef min
#undef max          // reclaim std::min / std::max
```

Ugly and necessary. It is also a good argument for `#define NOMINMAX` *before* the include —
prevention over cure.

---

## Checkpoint

- [ ] Reproduce the `int MAX_SIZE;` error. Note where the compiler points
- [ ] Convert a `#define` constant in your own code to `inline constexpr` and confirm it still
      compiles
- [ ] Write `#define PAGE_SZ 4096` and compute `pageId * PAGE_SZ` with `pageId = 1048576`.
      Confirm it overflows, and explain why the `constexpr` version does not
- [ ] Try `#if SomeConstexprBool` and observe that it silently evaluates to 0
- [ ] Define a macro named `min` before including `<algorithm>` and watch `std::min` break
- [ ] Answer: *why does a macro constant have no type, and when does that actually bite?*
- [ ] Answer: *what is the one thing `#define` can do that `inline constexpr` cannot?*

Next: [03 — Function-like Macros](03-function-like-macros.md), where the bugs get worse.
