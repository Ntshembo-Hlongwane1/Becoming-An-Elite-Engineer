# 11 — Real-World Patterns

> The macros you will actually meet, in real headers, doing real jobs. Each is a worked example
> of the techniques from docs 02–08 combined.

---

## 1. `assert` — the canonical macro

```cpp
// roughly what <cassert> does
#ifdef NDEBUG
  #define assert(e) ((void)0)
#else
  #define assert(e) ((e) ? (void)0 \
                         : __assert_fail(#e, __FILE__, __LINE__, __func__))
#endif
```

Four techniques, all required:

| Piece | Doc | Why a function cannot |
|---|---|---|
| `#e` | 07 §1 | the expression's **text** is gone by call time |
| `__FILE__`, `__LINE__` | 06 §3 | a function sees its own location |
| `#ifdef NDEBUG` | 04 | must generate **zero** code in release |
| `((void)0)` | 08 §5 | the disabled form must still be a valid expression |

### The consequence you must remember

**In a release build the entire expression disappears.**

```cpp
assert(Initialise());          // NEVER DO THIS -- Initialise() is not called in release
```

`storage/02` §2.4 flagged this. It is the single most damaging assert misuse, and it produces a
program that works in debug and fails in production.

### `static_assert` where you can

```cpp
static_assert(sizeof(Page) == PAGE_SIZE, "...");     // compile time, zero cost, always checked
assert(index < keys.size());                          // runtime, debug only
```

Prefer `static_assert` whenever the condition is a constant expression. It cannot be compiled
away, it costs nothing, and a bug that cannot compile cannot ship.

---

## 2. Include guards and header hygiene

Doc 05 §3. In practice:

```cpp
#pragma once                                  // your repo's choice -- correct here
```

For a header shipped to unknown consumers, belt and braces:

```cpp
#pragma once
#ifndef SEARCHENGINE_STORAGE_PAGE_HPP
#define SEARCHENGINE_STORAGE_PAGE_HPP
...
#endif
```

Both, because `#pragma once` is faster where supported and the guard is a fallback. Only worth
it for widely distributed headers.

---

## 3. Export / visibility macros

The pattern with genuinely no alternative.

```cpp
// SedbExport.hpp
#pragma once

#if defined(_WIN32)
  #if defined(SEDB_BUILDING_DLL)
    #define SEDB_API __declspec(dllexport)
  #elif defined(SEDB_USING_DLL)
    #define SEDB_API __declspec(dllimport)
  #else
    #define SEDB_API                       // static library
  #endif
#else
  #if defined(SEDB_BUILDING_DLL)
    #define SEDB_API __attribute__((visibility("default")))
  #else
    #define SEDB_API
  #endif
#endif
```

```cpp
class SEDB_API BufferPool { ... };
```

**Why nothing else works:** the *same declaration* must read `dllexport` when building the
library and `dllimport` when consuming it. No C++ construct can change a declaration's meaning
based on who is compiling it. This is legitimate use #2 (conditional compilation) applied to a
declaration rather than a block.

Note `SEDB_BUILDING_DLL` comes from the build system (§4) — CMake's
`target_compile_definitions(searchengine_core PRIVATE SEDB_BUILDING_DLL)`.

---

## 4. Build-system integration — `-D`

```bash
g++ -DSEDB_LOG_LEVEL=3 -DNDEBUG -DSEDB_ENABLE_STATS ...
```

```cmake
target_compile_definitions(searchengine_core
    PRIVATE SEDB_BUILDING_DLL
    PUBLIC  SEDB_LOG_LEVEL=$<IF:$<CONFIG:Debug>,3,1>
)
```

`-DNAME` defines it as `1`; `-DNAME=value` gives it a value. This is the **only channel** from
your build system into your source — a build variable cannot become a `constexpr` any other way.

Always give a default so the file compiles standalone:

```cpp
#ifndef SEDB_LOG_LEVEL
  #define SEDB_LOG_LEVEL 2
#endif
```

Without it, forgetting the flag silently means `#if SEDB_LOG_LEVEL >= 3` sees `0` (doc 04 §1)
and logging vanishes with no diagnostic.

---

## 5. The config header — turning macros back into C++

The pattern that limits macro blast radius: `-D` flags land in **one** header, which immediately
converts them into typed constants.

```cpp
// SedbConfig.hpp
#pragma once
#include <cstddef>

#ifndef SEDB_PAGE_SIZE
  #define SEDB_PAGE_SIZE 4096
#endif
#ifndef SEDB_ENABLE_STATS
  #define SEDB_ENABLE_STATS 0
#endif

namespace sedb::config {
    inline constexpr std::size_t PageSize    = SEDB_PAGE_SIZE;
    inline constexpr bool        EnableStats = SEDB_ENABLE_STATS != 0;
}
```

Now the rest of the codebase uses `sedb::config::PageSize` — typed, scoped, debuggable — and the
macros exist in exactly one file. Where you need compile-time branching:

```cpp
if constexpr (sedb::config::EnableStats) { ... }      // type-checked, both branches
```

**This is the pattern to adopt** if `searchengine` ever grows build-time options. It gives you
the build-system integration macros are required for, without letting them spread.

CMake's `configure_file` generates such a header from a `.in` template, which is how most
projects do it.

---

## 6. Compiler attribute portability

```cpp
#if defined(__GNUC__) || defined(__clang__)
  #define SEDB_ALWAYS_INLINE inline __attribute__((always_inline))
  #define SEDB_NOINLINE      __attribute__((noinline))
  #define SEDB_LIKELY(x)     __builtin_expect(!!(x), 1)
#elif defined(_MSC_VER)
  #define SEDB_ALWAYS_INLINE __forceinline
  #define SEDB_NOINLINE      __declspec(noinline)
  #define SEDB_LIKELY(x)     (x)
#else
  #define SEDB_ALWAYS_INLINE inline
  #define SEDB_NOINLINE
  #define SEDB_LIKELY(x)     (x)
#endif
```

Note the `else` branch degrades gracefully rather than failing — correct for *optimisation*
hints, wrong for *correctness* features (where `#error` is right, doc 10 §4).

Standard attributes need no macro: `[[nodiscard]]`, `[[maybe_unused]]`, `[[fallthrough]]`, and
C++20's `[[likely]]`/`[[unlikely]]` are portable. Use `__has_cpp_attribute` (doc 06 §7) if you
must support older compilers.

`[[nodiscard]]` is worth adopting in your storage layer — on `BufferPool::FetchPage`, ignoring
the returned `Page*` is always a bug, and on `PageGuard`-returning factories, discarding the
guard unpins immediately.

---

## 7. Deprecation and migration

```cpp
#define SEDB_DEPRECATED(msg) [[deprecated(msg)]]

class SEDB_DEPRECATED("use BufferPool::FetchGuarded instead") LegacyFetcher { ... };
```

`[[deprecated]]` is standard; the macro only buys you a single place to disable it. Useful when
migrating a codebase in stages, which the storage series will do if you replace `FetchPage` with
`FetchGuarded` everywhere.

---

## 8. Patterns to recognise and avoid

**`#define private public`** — to test private members. Undefined behaviour (it changes class
layout rules), breaks the ODR, and fails unpredictably. Use `friend`, or test through the public
interface.

**`#define TRUE 1` / `#define FALSE 0`** — C++ has `true` and `false` with the right type.

**`#define NULL 0`** — use `nullptr`.

**`#define BEGIN {` / `#define END }`** — makes C++ look like Pascal. Every reader now needs
your dialect.

**Macros wrapping `new`/`delete` for leak tracking** — breaks placement new, breaks
`operator new` overloads, breaks alignment (`storage/02` §2.3). Use a sanitizer or a real
allocator hook — the technique from the rule-of-five series' leak check.

**Header-only libraries that `#define` common words** — the `<windows.h>` `min`/`max` problem.
If you must, `#undef` immediately after including.

---

## Checkpoint

- [ ] Read `<cassert>` on your system and confirm the structure in §1
- [ ] Write `assert(SideEffect())`, build with `-DNDEBUG`, and confirm via `-E` that the call
      vanishes
- [ ] Create `SedbConfig.hpp` per §5; drive `PageSize` from a `-D` flag and confirm it changes
- [ ] Add `[[nodiscard]]` to `BufferPool::FetchPage` and find the call sites that now warn
- [ ] Write the `SEDB_API` block and reason about which branch a static build takes
- [ ] Add `#ifndef` defaults to every `-D`-driven macro you introduce
- [ ] Answer: *why must an export macro be a macro?*
- [ ] Answer: *what does the config-header pattern buy over using `-D` macros directly?*

Next: [12 — Checklist & Antipatterns](12-checklist.md).
