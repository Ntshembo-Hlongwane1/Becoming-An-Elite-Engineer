# 04 — Conditional Compilation

> **The one job no other C++ feature can do.** This is the directive in your `DiskManager.cpp`,
> and the only macro use in this series that has no modern replacement.
>
> It also carries the hazard that prompted the whole series: **the branch you did not take is
> never parsed, never type-checked, and never seen by any warning.**

---

## 1. The directives

```cpp
#if   EXPRESSION        // integer constant expression
#ifdef  NAME            // shorthand for #if defined(NAME)
#ifndef NAME            // shorthand for #if !defined(NAME)
#elif EXPRESSION
#else
#endif
```

The preprocessor evaluates the condition and **deletes the losing text entirely**. It never
reaches the compiler.

### `#if defined(X)` versus `#ifdef X`

Identical for a single name. `defined()` wins when you need to combine:

```cpp
#if defined(__linux__) || defined(__APPLE__)
#if defined(_WIN32) && !defined(_WIN64)
```

`#ifdef` cannot express either. **Prefer `#if defined(X)` uniformly** — one form to read, and it
scales to compound conditions. That is what `storage/03` uses.

### `#if` on an undefined name is silently zero

```cpp
#if ENABLE_STATS        // ENABLE_STATS is not defined anywhere
    // this is silently SKIPPED. No warning.
#endif
```

Undefined identifiers in `#if` evaluate to `0`. Combined with a typo:

```cpp
#if defined(ENABLE_STATSS)      // typo -- silently false, forever
```

**This is the most common conditional-compilation bug**, and the defence is `-Wundef`:

```bash
g++ -Wundef ...      # warns when an undefined identifier is used in #if
```

Not in `-Wall` or `-Wextra`. Turn it on. Note it warns for `#if FOO` but not for
`#if defined(FOO)` — the latter is a legitimate existence test.

---

## 2. The hazard: the inactive branch is not code

```cpp
#if defined(_WIN32)
    printf("windows branch\n");
#else
    this is not even valid C++ !!! @@@ ###
#endif
```

Compiled on Windows with `-Wall -Wextra`:

```
   build exit: 0
```

**Clean build.** No error, no warning, nothing. The `#else` text was deleted in phase 4, so the
compiler never saw it.

Consequences you must plan around:

- **Syntax errors hide indefinitely** in branches you do not build.
- **Type errors, missing includes, renamed functions** — all invisible.
- **Refactoring tools miss it.** Rename a function and the `#else` branch keeps the old name.
- **Coverage and static analysis see one branch**, because they run after the preprocessor.

This is why `storage/03` §9.1 warns that "a syntax error inside the Linux branch will not be
caught by your Windows build. It compiles for you and breaks for the next person."

### Mitigations

**Compile both branches somewhere.** CI on both platforms is the only real answer. Failing that,
force the other branch locally to at least parse:

```bash
g++ -U_WIN32 -fsyntax-only file.cpp      # try the non-Windows path
```

It will fail on the missing platform headers, but it catches your own typos before the headers
do.

**Minimise the conditional region.** The best pattern puts the `#if` around *one small
definition* and keeps all logic outside it:

```cpp
// GOOD: 6 conditional lines, one shared implementation
#if defined(_WIN32)
  #define PORTABLE_FSEEK _fseeki64
#else
  #define PORTABLE_FSEEK fseeko
#endif

int DiskManager::Seek(std::int64_t off) {           // compiled on every platform
    return PORTABLE_FSEEK(m_File, off, SEEK_SET);
}
```

```cpp
// BAD: two whole implementations, only one ever compiled
#if defined(_WIN32)
    int DiskManager::Seek(std::int64_t off) { /* 40 lines */ }
#else
    int DiskManager::Seek(std::int64_t off) { /* 40 different lines */ }
#endif
```

The first has six lines that can rot. The second has forty. **Push the conditional down to the
smallest possible unit** — usually a typedef, a `#define` of a function name, or a single
`#include`.

**Better still: isolate behind a normal C++ interface.** One header declaring
`std::int64_t PlatformSeek(FILE*, std::int64_t)`, and two `.cpp` files chosen by your build
system. Now each platform's code is *ordinary C++* that its own compiler fully checks, and
CMake picks the file. No `#if` at all in your logic. That is the design to aim for as the
platform surface grows.

---

## 3. `#if` cannot see C++

```cpp
#if sizeof(int) == 4              // ERROR
#if MyConstexprValue > 10         // silently 0
#if std::is_same_v<T, int>        // ERROR
```

`#if` runs before the compiler exists. It handles integer literals, arithmetic, comparison,
`&&`/`||`/`!`, and `defined()`. Nothing else.

The C++-level equivalents run *after* parsing, and are the right tool when both branches are
valid C++:

```cpp
if constexpr (sizeof(void*) == 8) { ... } else { ... }   // both branches must PARSE
static_assert(sizeof(int) == 4, "...");                   // assert, don't branch
```

**The dividing line:** if both branches are valid C++ everywhere, use `if constexpr` and get full
type checking on both. If one branch cannot even *parse* on the other platform — because it
names `<unistd.h>` functions that do not exist there — you need `#if`.

---

## 4. The standard detection macros

You do not define these; the compiler does. Doc 06 covers the full set; these are the ones for
conditional compilation.

**Platform:**

| Macro | Means |
|---|---|
| `_WIN32` | any Windows, **including 64-bit** |
| `_WIN64` | 64-bit Windows (implies `_WIN32`) |
| `__linux__` | Linux |
| `__APPLE__` | macOS/iOS |
| `__unix__` | any Unix-like |

Verified on your toolchain:

```
#define _WIN32 1
#define _WIN64 1
```

**`_WIN32` is defined on 64-bit Windows.** The name is historical. Testing `#ifdef _WIN32` to
mean "32-bit" is a real and common bug; use `_WIN64` for bitness, or better
`sizeof(void*)`/`__SIZEOF_POINTER__`.

**Compiler:**

| Macro | Compiler |
|---|---|
| `__GNUC__` | GCC — **also defined by Clang** |
| `__clang__` | Clang — test this *first* |
| `_MSC_VER` | MSVC |
| `__MINGW32__` / `__MINGW64__` | MinGW |

The Clang/GCC overlap matters: Clang defines `__GNUC__` for compatibility, so
`#if defined(__GNUC__)` is true on both. Order your checks Clang-first.

**Language level:**

```cpp
#if __cplusplus >= 202002L      // C++20
```

Verified: `__cplusplus = 202002L` on your build. On MSVC this is stuck at `199711L` unless you
pass `/Zc:__cplusplus` — a notorious trap; use `_MSVC_LANG` there.

---

## 5. Feature-test macros — the modern, precise way

Rather than guessing capability from compiler version, C++20 standardised per-feature macros:

```cpp
#include <version>              // all library feature-test macros

#if __cpp_lib_source_location >= 201907L
    #include <source_location>
    // use it
#else
    #define CURRENT_LINE __LINE__
#endif
```

`__has_include` is the other precise tool:

```cpp
#if __has_include(<unistd.h>)
    #include <unistd.h>
    #define HAVE_UNISTD 1
#endif
```

**Prefer feature detection over platform detection.** "Does this header exist?" and "is this
feature available?" are the actual questions; "is this Windows?" is a proxy that goes wrong on
Cygwin, MinGW, WSL, and every future platform.

---

## 6. Include guards — the other universal use

```cpp
#ifndef SEARCHENGINE_PAGE_HPP
#define SEARCHENGINE_PAGE_HPP
// ...
#endif
```

Conditional compilation preventing double inclusion. Doc 05 covers this and `#pragma once` in
full; noting here that it is the same mechanism.

---

## 7. Commenting out code — don't use `#if 0`, and don't use comments either

```cpp
#if 0
    // disabled code
#endif
```

Better than `/* */` (which cannot nest), and it survives code containing `*/`. But **both are
worse than deleting the code.** You have version control; dead code in a file is dead weight
that no one dares remove because no one knows why it is there.

Legitimate short-term use: bisecting a build failure. Delete it before committing.

---

## Checkpoint

- [ ] Reproduce the invalid-`#else` build. Confirm exit 0 with `-Wall -Wextra`
- [ ] Add `-Wundef` and introduce a typo'd `#if ENABLE_STATSS`; confirm it now warns
- [ ] Run `g++ -U_WIN32 -fsyntax-only` on `DiskManager.cpp` and see how far it gets
- [ ] Check whether `_WIN32` is defined on your 64-bit build (it is — verify it)
- [ ] Try `#if sizeof(int) == 4` and read the error
- [ ] Rewrite one `#if` region to be smaller — ideally one `#define`, not a whole function
- [ ] Answer: *when should you use `if constexpr` instead of `#if`?*
- [ ] Answer: *why prefer `__has_include(<unistd.h>)` over `#ifdef __linux__`?*

Next: [05 — `#include` Mechanics](05-include-mechanics.md).
