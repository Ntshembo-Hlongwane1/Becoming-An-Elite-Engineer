# 12 — Checklist and Antipatterns

> The series compressed: one question, one flowchart, the bug catalogue, the flags, and a
> one-page summary.

---

## 1. The one question

> **What can a macro do here that a function, a template, or a `constexpr` cannot?**

If you cannot answer in a sentence, do not write the macro.

```
                 Can a function / template / constexpr do it?
                        /                       \
                      YES                       NO
                       |                         |
                 ┌──────────┐        Must it act BEFORE parsing?
                 │ USE THAT │         (include guard, platform #include,
                 └──────────┘          export decoration, -D flag)
                                        /                  \
                                      YES                  NO
                                       |                    |
                                 ┌──────────┐    Does it need the CALLER's line,
                                 │  MACRO   │    or an expression's TEXT?
                                 └──────────┘      /              \
                                                 YES              NO
                                                  |                |
                                     ┌────────────────────┐  ┌───────────┐
                                     │ source_location if │  │ NOT a     │
                                     │ enough, else MACRO │  │ macro     │
                                     └────────────────────┘  └───────────┘
```

---

## 2. If you write one — the checklist

- [ ] `SCREAMING_SNAKE_CASE` with a project prefix (`SEDB_`)
- [ ] Never a name starting with `_` or containing `__` — reserved (doc 06 §8)
- [ ] Every parameter parenthesised, **and** the whole body (doc 03 §2)
- [ ] Each parameter used **exactly once**, or documented as requiring side-effect-free arguments
- [ ] Multi-statement bodies wrapped in `do { } while(0)` (doc 03 §4)
- [ ] Disabled forms expand to `((void)0)`, never nothing (doc 08 §5)
- [ ] `__VA_OPT__(,)` rather than the GNU `, ##__VA_ARGS__` (doc 08 §3)
- [ ] `#ifndef` default for anything driven by `-D` (doc 11 §4)
- [ ] `#error` in the `#else` of a platform check that cannot degrade (doc 10 §4)
- [ ] No `return` / `break` / `continue` inside — they escape into the caller
- [ ] Expansion verified with `g++ -E -P`
- [ ] A comment saying **why a function would not work**

---

## 3. The antipattern catalogue

| # | Antipattern | Symptom | Doc |
|---|---|---|---|
| A | Unparenthesised parameters | `SQUARE(n+1)` → 9 instead of 25 | 03 §2 |
| B | Unparenthesised body | `10 * ADD(1,2)` → 12 instead of 30 | 03 §2 |
| C | Parameter used twice | `MAX(a++,b++)` increments twice | 03 §3 |
| D | Multi-statement without `do/while(0)` | `'else' without a previous 'if'` | 03 §4 |
| E | Comma inside a template argument | "passed 3 arguments, but takes 2" | 03 §5 |
| F | `#define` for a typed constant | wrong-width arithmetic, no scope, no debugger | 02 |
| G | Macro name collides with an identifier | `expected unqualified-id` at the `#define` | 02 §2.1 |
| H | Typo'd `#if FOO` | silently `0`, branch never taken | 04 §1 |
| I | Large `#if` regions | untaken branch rots invisibly | 04 §2 |
| J | Side effects inside `assert` | works in debug, breaks in release | 11 §1 |
| K | Missing `#ifndef` default for a `-D` macro | flag forgotten → silent `0` | 11 §4 |
| L | `#define private public` | UB, ODR violation, layout changes | 11 §8 |
| M | Reserved names (`_FOO`, `__bar`) | collides with compiler internals | 06 §8 |
| N | Forgetting `#undef X` after an X-macro | next use expands bizarrely | 07 §4 |

---

## 4. Flags

```bash
g++ -std=c++20 -Wall -Wextra \
    -Wundef \             # undefined identifier in #if  -- antipattern H
    -Wshadow \
    -Wsequence-point \    # multiple evaluation -- antipattern C (in -Wall)
    file.cpp
```

**`-Wundef` is the important one and is not in `-Wall` or `-Wextra`.** It catches the silent-zero
bug, which is the most common conditional-compilation failure. Note it warns for `#if FOO` but
correctly stays quiet for `#if defined(FOO)`.

Diagnostic commands, all from doc 10:

```bash
g++ -E -P file.cpp                    # what the compiler actually sees
g++ -dM -E -x c++ - < /dev/null       # every predefined macro (490 on your box)
g++ -dM -E file.cpp | grep NAME       # is NAME a macro? what is it?
g++ -save-temps -c file.cpp           # leaves file.ii for inspection
g++ -U_WIN32 -fsyntax-only file.cpp   # try the other platform branch
```

---

## 5. Applying it to `searchengine`

The audit from doc 09 §7, as actions:

- [ ] **Nothing to fix.** `PORTABLE_FSEEK`, `#pragma once`, `PAGE_SIZE` as `inline constexpr`,
      and `NodeType` as `enum class` are all correct choices.
- [ ] Add `-Wundef` to `CMakeLists.txt`
- [ ] Add an `#error` to the `#else` of `DiskManager.cpp`'s platform check, so an unsupported
      platform fails loudly instead of assuming POSIX
- [ ] Add the little-endian `#error` guard from doc 06 §5 to `Page.hpp` — the page format assumes
      it and nothing currently checks
- [ ] If you add logging, use doc 08 §5's shape: `do/while(0)`, `__VA_OPT__`, `((void)0)` when
      disabled, `SEDB_` prefix
- [ ] If build options ever appear, use doc 11 §5's config-header pattern rather than scattering
      `-D` macros

---

## 6. The series in one page

| Doc | The thing to remember |
|---|---|
| 01 | The preprocessor is text substitution running **before** the compiler. `g++ -E` shows you everything |
| 02 | `#define` constants have no scope, no type, and no debugger. `inline constexpr` has all three |
| 03 | Macro arguments are **tokens**, not values — hence precedence bugs and double evaluation |
| 04 | Only `#if` can delete code before parsing. The untaken branch is **never checked** |
| 05 | `#include` is a paste. Your 140-line header expands to 50,166 lines |
| 06 | You already have 490 macros. `__FILE__`/`__LINE__` are the two functions cannot replace |
| 07 | `#` captures an expression's **text**; `##` builds identifiers. X-macros keep lists in sync |
| 08 | `__VA_OPT__(,)` fixes the dangling comma. Disabled macros must be `((void)0)` |
| 09 | Four legitimate uses: guards, conditional compilation, call-site capture, code generation |
| 10 | When a macro misbehaves, read the **expansion**, not the macro. Errors read bottom-up |
| 11 | `assert`, export macros, and `-D` flags are the patterns with no alternative |
| 12 | Ask: *what can a macro do here that a function cannot?* |

---

## 7. Where to go next

- **C++20 modules** — retires include guards and the 50,166 lines. `import std;` when your
  toolchain is ready.
- **`std::source_location`** — retires `__FILE__`/`__LINE__` for logging (doc 06 §3).
- **`std::format`** — type-safe formatting, retiring most `printf`-shaped logging macros.
- **C++26 reflection** — the eventual replacement for X-macros.
- **Boost.Preprocessor** — if you ever *must* do heavy preprocessor metaprogramming. Read it
  once to see how far the technique goes, then avoid needing it.

Back to [00 — Index](00-index.md).
