# Macros and the Preprocessor

> **Goal:** finish this series knowing exactly what the preprocessor does, why the four
> legitimate uses of macros are legitimate, why everything else has a better modern replacement,
> and how to debug a macro when it goes wrong — which it will, in ways the compiler cannot help
> you with.
>
> **Everything here is measured.** Every claim is backed by a program compiled and run on your
> toolchain (GCC 16, C++20, MinGW-w64), including the preprocessor's own output via `g++ -E`.

---

## Why you are reading this

From `lesson doc/storage/03-diskmanager-io.md`:

```cpp
#if defined(_WIN32)
  #include <io.h>
  #define PORTABLE_FSEEK  _fseeki64
#else
  #include <unistd.h>
  #define PORTABLE_FSEEK  fseeko
#endif
```

Six lines that are not C++. They are a *different language*, processed by a *different program*,
before the C++ compiler sees anything. That doc flagged one hazard in passing — the inactive
branch is never type-checked — and here is the proof:

```cpp
#if defined(_WIN32)
    printf("windows branch\n");
#else
    this is not even valid C++ !!! @@@ ###
#endif
```

```
=== a syntactically INVALID inactive branch still compiles on Windows ===
   build exit: 0
```

Compiled clean with `-Wall -Wextra`. The garbage in the `#else` is not a syntax error, not a
warning, not anything — because **the compiler never sees it.** Ship that and it breaks on the
first Linux build, and the person it breaks for did not write it.

That is the preprocessor: enormously powerful, completely unaware of C++, and invisible in every
tool you normally rely on.

---

## The one sentence

> **The preprocessor is a text substitution engine that runs before the compiler. It does not
> know about types, scopes, namespaces, functions, or classes. It knows tokens and text.**

Every property in this series — good and bad — follows from that sentence. Macros ignore scope
because the preprocessor has no concept of scope. Macros produce baffling errors because the
compiler reports on text you never wrote. Macros are the only tool for conditional compilation
because they are the only thing that runs early enough to *remove* code.

---

## The series

| Doc | Title | The question it answers |
|---|---|---|
| [01](01-what-the-preprocessor-is.md) | What the Preprocessor Is | What actually happens before compilation? *(with `-E` output)* |
| [02](02-object-like-macros.md) | Object-like Macros | `#define PI 3.14` — and why `constexpr` beats it on five counts |
| [03](03-function-like-macros.md) | Function-like Macros | The parenthesis bug, double evaluation, `do/while(0)` |
| [04](04-conditional-compilation.md) | Conditional Compilation | `#if` / `#ifdef` — the one job only macros can do |
| [05](05-include-mechanics.md) | `#include` Mechanics | Guards, `#pragma once`, include order, self-contained headers |
| [06](06-predefined-macros.md) | Predefined Macros | The 490 macros you already have; platform and compiler detection |
| [07](07-stringify-and-paste.md) | Stringify `#` and Paste `##` | Token manipulation, and the two-level expansion trick |
| [08](08-variadic-macros.md) | Variadic Macros | `__VA_ARGS__`, `__VA_OPT__`, and building a logger |
| [09](09-when-macros-are-correct.md) | When a Macro Is the Right Answer | The four legitimate uses, and the replacement for everything else |
| [10](10-debugging-macros.md) | Debugging Macros | `-E`, `-dM`, and decoding the error messages |
| [11](11-real-world-patterns.md) | Real-World Patterns | `assert`, logging, export macros, build-system `-D`, config headers |
| [12](12-checklist.md) | Checklist & Antipatterns | The decision flowchart and the bug catalogue |

---

## The headline results

Three things measured in this series that are worth knowing before you start.

**Macros are textual, and text does not respect arithmetic.**

```cpp
#define SQUARE_BAD(x) x * x
SQUARE_BAD(n + 1)        //  expands to:  n + 1 * n + 1
```
```
n = 4
  SQUARE_BAD(n+1) = 9   (expected 25)
```

**Macro arguments can be evaluated more than once.**

```cpp
#define MAX(a,b) ((a) > (b) ? (a) : (b))
MAX(a++, b++)            //  expands to:  ((a++) > (b++) ? (a++) : (b++))
```
```
a=3 b=4
  MAX(a++,b++) = 5  -> a=4 b=6   (b incremented TWICE)
```

**You already have 490 of them.**

```
$ echo | g++ -std=c++20 -dM -E -x c++ - | wc -l
490
```

Before you write a single line, the preprocessor has defined `_WIN32`, `__GNUC__`,
`__cplusplus`, `__x86_64__`, `__BYTE_ORDER__`, and 485 others. Doc 06 is about which ones you
can rely on.

---

## The four legitimate uses

Everything in doc 09, stated up front so you can read the rest with the conclusion in view:

1. **Include guards** — `#pragma once` or `#ifndef`. Nothing else can do this.
2. **Conditional compilation** — platform, build config, feature detection. Nothing else can
   *remove* code before it is parsed.
3. **Capturing source location** — `__FILE__`, `__LINE__`. A function cannot know its caller's
   line number. (C++20's `std::source_location` finally replaces this.)
4. **Code generation that the language cannot express** — X-macros, and the boilerplate in
   logging and assertion macros.

**Everything else has a better replacement**, and doc 02 §4 has the table: `constexpr` for
constants, `inline` functions and templates for function-like macros, `enum class` for
constant sets, `constexpr if` for compile-time branching.

---

## How to read this

Front to back, with a terminal open. The single most valuable habit this series teaches is:

```bash
g++ -std=c++20 -E -P yourfile.cpp | less
```

**When a macro misbehaves, look at what it expanded to.** Not at the macro. Not at the error
message. At the text the compiler actually received. Doc 10 is entirely about this.

## Prerequisites

- `lesson doc/storage/03-diskmanager-io.md` §9.1 — the platform conditionals this series
  explains properly.
- `lesson doc/storage/02-the-page.md` §1 — `#pragma once` and `inline constexpr`, which doc 02
  and doc 05 here revisit in depth.
