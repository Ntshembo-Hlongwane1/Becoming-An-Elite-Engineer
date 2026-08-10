# 09 — When a Macro Is the Right Answer

> Eight docs of hazards. This one is the constructive half: the **four** things only macros can
> do, and the modern replacement for everything else.
>
> The test to apply, every time: **"what can a macro do here that a function, a template, or a
> `constexpr` cannot?"** If the answer is "nothing", you have your answer.

---

## 1. Legitimate use #1 — include guards

```cpp
#pragma once
// or
#ifndef SEARCHENGINE_STORAGE_PAGE_HPP
#define SEARCHENGINE_STORAGE_PAGE_HPP
...
#endif
```

**Why nothing else works:** preventing double *inclusion* requires acting during inclusion,
which is phase 4. No C++ construct exists at that point.

**Replacement:** C++20 modules. `export module searchengine.storage.page;` is parsed once, and
the whole problem disappears. Toolchain support is still uneven; until then, guards.

---

## 2. Legitimate use #2 — conditional compilation

```cpp
#if defined(_WIN32)
  #include <io.h>
  #define PORTABLE_FSEEK _fseeki64
#else
  #include <unistd.h>
  #define PORTABLE_FSEEK fseeko
#endif
```

**Why nothing else works:** `if constexpr` requires **both branches to parse**. On Windows,
`#include <unistd.h>` is not a compile error you can branch around — the file does not exist, and
the include is resolved before any C++ rule applies. Only text deletion can handle it.

Doc 04 §2's rule stands: make the conditional region as small as possible, and prefer isolating
platform code behind a normal C++ interface with the build system choosing the `.cpp`.

**Replacement:** none for the header case. For *behaviour* that is valid everywhere,
`if constexpr` and `std::endian` are strictly better.

---

## 3. Legitimate use #3 — capturing the call site

```cpp
#define ASSERT(e) ((e) ? (void)0 : Fail(#e, __FILE__, __LINE__))
#define LOG(fmt, ...) printf("[%s:%d] " fmt "\n", __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
```

**Why nothing else works (pre-C++20):** a function sees its own `__LINE__`. And `#e` — the
*text* of the expression — is gone by the time a function is called.

**Replacement:** `std::source_location` (C++20) retires the location half:

```cpp
void Log(std::string_view msg,
         std::source_location loc = std::source_location::current());
```

The expression-text half (`#e`) still has no replacement, so `assert`-style macros survive.
C++26's reflection may change that.

---

## 4. Legitimate use #4 — code the language cannot generate

**X-macros** (doc 07 §4), when one list drives three or more artefacts and drift is a real risk.

**Unique identifier generation:**

```cpp
#define SCOPED_TIMER(name) Timer SEDB_UNIQUE(timer_)(name)
```

A template cannot invent a variable name; only `##` with `__LINE__` or `__COUNTER__` can.

**Replacement:** none today. C++26 reflection is the eventual answer for X-macros.

---

## 5. Everything else — the replacement table

| Macro use | Replacement | Why better |
|---|---|---|
| `#define PI 3.14` | `inline constexpr double Pi` | typed, scoped, debuggable (doc 02) |
| `#define MAX_SIZE 100` | `inline constexpr std::size_t` | correct arithmetic width |
| `#define RED 0` `GREEN 1` | `enum class Colour` | one type, no implicit `int` |
| `#define SQUARE(x) ((x)*(x))` | `constexpr` function | single evaluation (doc 03) |
| `#define MAX(a,b) ...` | `std::max` / template | already exists, correct |
| `#define SWAP(a,b) ...` | `std::swap` | ADL, move-aware |
| `#define ARRAY_LEN(a)` | `std::size(a)` | works on containers, cannot decay |
| Type aliases via macro | `using` | scoped, templatable |
| Generic algorithms | templates | type checking |
| Inlining for speed | `inline` / the optimiser | better judgement than yours |
| `#define DEBUG_ONLY(x)` | `if constexpr` | type-checked |
| `#ifdef` for platform *behaviour* | `if constexpr`, `std::endian` | both branches checked |

---

## 6. The decision procedure

```
                 Can a function, template, or constexpr do this?
                         /                        \
                       YES                        NO
                        |                          |
                  ┌──────────┐          Does it need to run BEFORE parsing?
                  │ USE THAT │            (include guard, platform #include)
                  └──────────┘              /                    \
                                          YES                    NO
                                           |                      |
                                    ┌─────────────┐      Does it need the CALLER's
                                    │ MACRO — the │      line, or an expression's TEXT?
                                    │ only option │        /                \
                                    └─────────────┘      YES                NO
                                                          |                  |
                                              ┌────────────────────┐  ┌────────────┐
                                              │ source_location if │  │ You do not │
                                              │ enough; else MACRO │  │ need a macro│
                                              └────────────────────┘  └────────────┘
```

---

## 7. Applying it to your repo

Your existing macro usage, audited:

**`storage/03` — `PORTABLE_FSEEK` / `PORTABLE_FTELL`** ✔ Legitimate use #2. The two functions
have genuinely different names on the two platforms and the headers differ. Correct, minimal
(two `#define`s rather than two implementations), and prefixed.

**`storage/02` — `#pragma once`** ✔ Legitimate use #1.

**`storage/02` — `PAGE_SIZE` as `inline constexpr`, not `#define`** ✔ The right call, and doc 02
§2.2 shows what the macro version would have cost: `pageId * PAGE_SIZE` computed in 32 bits,
overflowing at 4 GB.

**`storage/05` — `NodeType` as `enum class`, not `#define NODE_LEAF 1`** ✔ Fixed width for the
page format, no implicit `int`.

**`storage/11` — the proposed `Crc32` and header-generation code** — no macros needed.

**Nothing in `internal/kernal/` currently misuses a macro.** That is worth noting: the audit
finds nothing to fix. The value of this series for you is not repair, it is being able to *say
why* each of those choices is right, and to recognise the pressure when someone proposes
`#define LOG(...)`.

---

## 8. The counter-argument, stated fairly

Real codebases use far more macros than this doc endorses, and not always wrongly:

- **Compile-time removal.** A disabled logging macro generates *zero* code. A disabled function
  call relies on the optimiser, which usually delivers but is not guaranteed, and never in a
  debug build. For a trace macro in a hot loop this is a real difference.
- **Cross-language headers.** A header shared with C cannot use templates or `constexpr`.
- **ABI and export decorations.** `SEDB_API` expanding to `__declspec(dllexport)` or nothing has
  no alternative (doc 11 §3).
- **Build-system integration.** `-DENABLE_STATS=1` reaching the source can only arrive as a
  macro.

The honest position is not "macros are bad." It is: **macros are a code generator with no type
system, so use them where generation is genuinely required and nowhere else.**

---

## Checkpoint

- [ ] Grep your repo for `#define` and classify every hit against §6's flowchart
- [ ] Find one macro in a third-party header you use, and decide whether it is justified
- [ ] Convert one function-like macro you have read into a `constexpr` function
- [ ] Write the `std::source_location` logger and identify what it still cannot replace
- [ ] Answer: *why can't `if constexpr` replace a platform `#include`?*
- [ ] Answer: *what are the four things only a macro can do?*

Next: [10 — Debugging Macros](10-debugging-macros.md).
