# 06 — Predefined Macros

> Before you write a line, your compiler has defined **490 macros**. They are how you detect the
> platform, the compiler, the language level, and the build configuration — and two of them do
> something no C++ function can.

---

## 1. Seeing all of them

```bash
echo | g++ -std=c++20 -dM -E -x c++ -
```

- `-dM` — dump the macro definitions instead of the preprocessed text
- `-E` — preprocess only
- `-x c++ -` — treat stdin as C++

On your toolchain:

```
$ echo | g++ -std=c++20 -dM -E -x c++ - | wc -l
490
```

A sample:

```
#define _WIN32 1
#define _WIN64 1
#define __BYTE_ORDER__ __ORDER_LITTLE_ENDIAN__
#define __GNUC__ 16
#define __SIZEOF_POINTER__ 8
#define __cplusplus 202002L
#define __x86_64__ 1
```

Run this whenever you are unsure what is available. It is the authoritative answer for *your*
compiler, which beats any table including this one.

---

## 2. The standard ones

Defined by the C++ standard itself, so portable everywhere.

| Macro | Expands to | Note |
|---|---|---|
| `__FILE__` | source file name, as a string | see §3 |
| `__LINE__` | current line number, as an int | see §3 |
| `__DATE__` | `"Aug 10 2026"` | compile date — wrecks reproducible builds |
| `__TIME__` | `"14:32:01"` | same |
| `__cplusplus` | `202002L` for C++20 | §4 |
| `__STDC_HOSTED__` | 1 for a hosted implementation | |

And `__func__`, which is **not** a macro — it is a predefined `static const char[]` inside every
function. That distinction matters: it is scoped, typed, and obeys C++ rules.

```
__FILE__    = C:/.../p5.cpp
__LINE__    = 10
__func__    = main
__cplusplus = 202002
```

---

## 3. `__FILE__` and `__LINE__` — the one thing functions cannot do

```cpp
void log(const char* msg) {
    printf("[%s:%d] %s\n", __FILE__, __LINE__, msg);   // ALWAYS log.cpp:2
}
```

A function sees **its own** location. Every call reports the same line, which is useless.

```cpp
#define LOG(msg) printf("[%s:%d] %s\n", __FILE__, __LINE__, msg)
```

The macro expands **at the call site**, so `__FILE__` and `__LINE__` are the caller's. This is
legitimate use #3 from doc 00, and until C++20 there was no alternative.

It is why `assert` is a macro:

```cpp
#define assert(e) ((e) ? (void)0 : _assert_fail(#e, __FILE__, __LINE__))
```

It needs the caller's location **and** the expression's text (doc 07). Neither is available to a
function.

### The C++20 replacement

```cpp
#include <source_location>

void log(const char* msg,
         const std::source_location loc = std::source_location::current()) {
    printf("[%s:%d] %s in %s\n",
           loc.file_name(), loc.line(), msg, loc.function_name());
}

log("hello");        // reports the CALLER's location
```

The trick is that a **default argument is evaluated at the call site**, so
`source_location::current()` captures the caller. This finally retires the macro for logging —
it is a real function, so it is scoped, typed, debuggable, and overloadable.

Guard it with a feature test (doc 04 §5):

```cpp
#if __cpp_lib_source_location >= 201907L
```

---

## 4. `__cplusplus`

```cpp
#if __cplusplus >= 202002L      // C++20
#if __cplusplus >= 201703L      // C++17
#if __cplusplus >= 201402L      // C++14
#if __cplusplus >= 201103L      // C++11
```

Verified `202002L` on your build.

**The MSVC trap:** MSVC reports `199711L` (C++98) regardless of `/std:`, unless you pass
`/Zc:__cplusplus`. Decades of code tested `__cplusplus` and broke on MSVC, so Microsoft froze it
for compatibility. The portable form:

```cpp
#if defined(_MSVC_LANG)
  #define CXX_VERSION _MSVC_LANG
#else
  #define CXX_VERSION __cplusplus
#endif
```

**Better: use feature-test macros instead.** `__cpp_lib_source_location`,
`__cpp_if_constexpr`, `__cpp_concepts` — from `<version>` — tell you what is actually
available, which is the real question. A language level is a proxy; compilers ship features
unevenly.

---

## 5. Platform and compiler detection

Covered in doc 04 §4; the essentials again with the traps.

```cpp
// Compiler -- order matters
#if   defined(__clang__)      // FIRST: Clang also defines __GNUC__
#elif defined(__GNUC__)
#elif defined(_MSC_VER)
#endif

// Platform
#if   defined(_WIN32)         // ALL Windows, including 64-bit
#elif defined(__linux__)
#elif defined(__APPLE__)
#endif
```

**`_WIN32` is defined on 64-bit Windows** — verified above, both `_WIN32` and `_WIN64` are `1`
on your build. For bitness use `_WIN64`, or better `__SIZEOF_POINTER__ == 8`, or best
`sizeof(void*) == 8` in a `static_assert` where the compiler can check it.

**Architecture and layout** — often more useful than the platform:

```
#define __x86_64__ 1
#define __SIZEOF_POINTER__ 8
#define __BYTE_ORDER__ __ORDER_LITTLE_ENDIAN__
```

That last one is directly relevant to your page format. `storage/02` §4 noted that `memcpy`
copies your machine's byte order and that cross-architecture files would need explicit
encoding. This is the macro that lets you assert it:

```cpp
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
  #error "searchengine page format assumes little-endian"
#endif
```

**Better where possible:** `std::endian::native == std::endian::little` from `<bit>`, usable in
a `static_assert`. Use the macro only when you must branch on it before parsing.

---

## 6. `NDEBUG` — the one you set

```cpp
#ifdef NDEBUG
    // release
#else
    // debug: asserts live
#endif
```

`NDEBUG` is defined by *you* (or your build system), not the compiler. `<cassert>` checks it:
when defined, `assert(x)` expands to nothing.

Verified on a default build:

```
NDEBUG not defined (debug build: asserts live)
```

CMake defines it automatically in `Release` and `RelWithDebInfo`. This is why `storage/02` §2.4
warns never to put side effects inside `assert` — in a release build the whole expression
vanishes.

---

## 7. `__has_include` and `__has_cpp_attribute`

Not macros exactly — preprocessor *operators*, usable only in `#if`.

```cpp
#if __has_include(<unistd.h>)
    #include <unistd.h>
    #define SEDB_HAVE_UNISTD 1
#endif

#if __has_cpp_attribute(likely)
    #define SEDB_LIKELY [[likely]]
#else
    #define SEDB_LIKELY
#endif
```

`__has_include` is the single best conditional-compilation tool added in the last decade,
because it asks the *actual question* — "can I include this?" — rather than inferring it from
the platform. It works correctly on Cygwin, MinGW, WSL, and platforms that did not exist when
your code was written.

---

## 8. Never define these yourself

Reserved to the implementation:

- Any name beginning with `_` followed by an uppercase letter
- Any name containing `__` anywhere
- At global scope, any name beginning with `_`

So `_MY_HEADER_H`, `__mylib_internal`, and `_Page` are all illegal, and all appear constantly in
real code. They usually work — until they collide with a compiler internal, at which point the
error is incomprehensible.

**Prefix with your project instead:** `SEDB_`, `SEARCHENGINE_`.

---

## Checkpoint

- [ ] Run the `-dM` dump; count yours and skim for anything surprising
- [ ] Find `__BYTE_ORDER__`, `__SIZEOF_POINTER__`, and `__GNUC__` in your dump
- [ ] Write the `LOG` macro and confirm it reports the *caller's* line; then write the function
      version and confirm it does not
- [ ] Implement the `std::source_location` version and compare
- [ ] Add the little-endian `#error` guard to `Page.hpp`; confirm it does not fire
- [ ] Build with `-DNDEBUG` and confirm your asserts vanish (check with `-E`)
- [ ] Answer: *why can't a function capture its caller's line number, and what changed in C++20?*
- [ ] Answer: *why is `__has_include` better than `#ifdef __linux__`?*

Next: [07 — Stringify and Paste](07-stringify-and-paste.md).
