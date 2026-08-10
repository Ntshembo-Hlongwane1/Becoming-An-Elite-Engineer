# 08 — Variadic Macros

> Macros taking a variable number of arguments. The mechanism behind every logging macro you
> have ever seen, and the source of one of the ugliest workarounds in C++ history — finally
> fixed in C++20.

---

## 1. `__VA_ARGS__`

```cpp
#define LOG(fmt, ...) printf("[%s:%d] " fmt "\n", __FILE__, __LINE__, __VA_ARGS__)

LOG("value = %d", 42);
```

`...` in the parameter list captures the remaining arguments; `__VA_ARGS__` expands to them,
**commas included**.

```
[.../p5.cpp:7] value = 42
```

Note `"[%s:%d] " fmt "\n"` — three adjacent string literals concatenated at compile time. This
only works because `fmt` must be a literal, which is a real constraint of the pattern and the
main reason `std::format` is nicer.

---

## 2. The dangling comma problem

```cpp
LOG("no args here");
```

`__VA_ARGS__` is empty, so this expands to:

```cpp
printf("[%s:%d] " "no args here" "\n", __FILE__, __LINE__, );
                                                          ^ trailing comma
```

Measured:

```
error: expected primary-expression before ')' token
    2 | #define LOG_OLD(fmt, ...) printf("[%d] " fmt "\n", __LINE__, __VA_ARGS__)
      |                                                                         ^
note: in expansion of macro 'LOG_OLD'
```

The macro works with arguments and fails without them. For a logging macro — where "just log
this message" is the most common call — that is unacceptable.

### The old workarounds, for reading legacy code

**GNU comma-swallowing** (an extension, adopted by Clang and MSVC eventually):

```cpp
#define LOG(fmt, ...) printf("[%d] " fmt "\n", __LINE__, ##__VA_ARGS__)
//                                                      ^^ deletes the preceding
//                                                         comma if __VA_ARGS__ is empty
```

Non-standard, produced a `-pedantic` warning, and behaved differently across compilers for
twenty years.

**Requiring at least one argument:**

```cpp
#define LOG(...) printf(__VA_ARGS__)
LOG("plain message");                    // works, but you lose the fmt/args split
```

---

## 3. `__VA_OPT__` — the C++20 fix

```cpp
#define LOG(fmt, ...) \
    printf("[%s:%d] " fmt "\n", __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
```

`__VA_OPT__(x)` expands to `x` **if `__VA_ARGS__` is non-empty**, and to nothing otherwise. So
the comma appears only when there is something after it.

Verified, both forms working from one definition:

```
[.../p5.cpp:6] no args here
[.../p5.cpp:7] value = 42
```

Standard, portable, and it does exactly what the GNU hack did without being an extension. **If
you are on C++20, use it.** Your build is `__cplusplus = 202002L`, so it is available.

`__VA_OPT__` can wrap any tokens, not just a comma:

```cpp
#define CALL(f, ...) f(__VA_OPT__(__VA_ARGS__))
#define TRACE(...)   Log("enter" __VA_OPT__(" with: ") __VA_ARGS__)
```

---

## 4. Counting arguments

There is no `__VA_COUNT__`. The standard trick:

```cpp
#define SEDB_NARG(...)  SEDB_NARG_(__VA_ARGS__, 5,4,3,2,1,0)
#define SEDB_NARG_(_1,_2,_3,_4,_5,N,...) N

SEDB_NARG(a,b,c)      // -> 3
```

**How it works:** the arguments shift the fixed list rightwards, and `N` lands on the count.
With three arguments, `_1,_2,_3` take `a,b,c`, then `_4=5`, `_5=4`, `N=3`.

It is clever, it caps at whatever length you write out, and it does not handle zero arguments
correctly without `__VA_OPT__`. Include it here so you recognise it in a library; **do not
write it yourself.** If you need to count arguments, you want a variadic *template*:

```cpp
template <typename... Args>
void Log(std::string_view fmt, Args&&... args) {
    constexpr std::size_t n = sizeof...(args);      // the real answer
}
```

---

## 5. A logging macro worth shipping

Combining docs 04, 06, 07, and this one:

```cpp
// SedbLog.hpp
#pragma once
#include <cstdio>

#ifndef SEDB_LOG_LEVEL
  #define SEDB_LOG_LEVEL 2          // 0=off 1=error 2=info 3=debug
#endif

#define SEDB_LOG_IMPL(tag, fmt, ...)                                   \
    do {                                                               \
        std::fprintf(stderr, "[" tag "] %s:%d: " fmt "\n",             \
                     __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__);    \
    } while (0)

#if SEDB_LOG_LEVEL >= 1
  #define SEDB_ERROR(fmt, ...) SEDB_LOG_IMPL("ERROR", fmt __VA_OPT__(,) __VA_ARGS__)
#else
  #define SEDB_ERROR(fmt, ...) ((void)0)
#endif

#if SEDB_LOG_LEVEL >= 3
  #define SEDB_DEBUG(fmt, ...) SEDB_LOG_IMPL("DEBUG", fmt __VA_OPT__(,) __VA_ARGS__)
#else
  #define SEDB_DEBUG(fmt, ...) ((void)0)
#endif
```

Every technique in the series is doing a job here:

| Technique | Doing what | Doc |
|---|---|---|
| `do { } while(0)` | one statement, safe in an `if` | 03 §4 |
| `__FILE__` / `__LINE__` | the **caller's** location | 06 §3 |
| `__VA_OPT__(,)` | works with and without arguments | §3 |
| `#if SEDB_LOG_LEVEL` | disabled levels **compile to nothing** | 04 |
| `((void)0)` | the disabled form is still a valid expression | below |
| `SEDB_` prefix | no collisions | 02 §5 |
| `#ifndef` default | overridable from the build system | 11 §4 |

**Why `((void)0)` and not empty:** an empty macro leaves `SEDB_DEBUG("x");` as a bare `;`, which
is legal but breaks `if (x) SEDB_DEBUG("y"); else ...`. `((void)0)` is a real expression that
does nothing, keeping the syntax valid everywhere. It also suppresses "statement has no effect"
warnings.

**Why the disabled version still takes arguments:** so that a typo inside a disabled log is
still a syntax error in the *macro invocation*. (It does not type-check the arguments — that
would require evaluating them. Argument rot inside disabled logging is a real and accepted cost.)

### The modern alternative

```cpp
#include <format>
#include <source_location>

template <typename... Args>
void LogInfo(std::format_string<Args...> fmt, Args&&... args,
             std::source_location loc = std::source_location::current());
```

Type-safe, no macro. **But it cannot be conditionally compiled away** — the call remains, and
you rely on the optimiser to delete an empty function body. For a debug-logging macro in a hot
path, that difference is the reason macros survive here. A hybrid is common: a macro whose body
calls the template, so you get type safety *and* removal.

---

## 6. Rules

- [ ] Use `__VA_OPT__(,)` rather than `, ##__VA_ARGS__` on C++20
- [ ] Wrap the body in `do { } while(0)`
- [ ] Make the disabled form `((void)0)`, never empty
- [ ] Prefix everything
- [ ] Prefer a variadic template unless you need `__LINE__` or compile-time removal
- [ ] Check the expansion with `-E` before trusting it

---

## Checkpoint

- [ ] Reproduce the dangling-comma error, then fix it with `__VA_OPT__`
- [ ] Build the logger from §5; verify with `-E` that `SEDB_DEBUG` vanishes at level 2
- [ ] Confirm `SEDB_DEBUG("x");` compiles inside `if (cond) ... else ...` when disabled
- [ ] Remove `do/while(0)` and find the construct that breaks
- [ ] Build `SEDB_NARG` and test it with 1, 3, and 5 arguments; then with 0
- [ ] Write the `std::format` + `source_location` version and compare
- [ ] Answer: *why does an empty macro body break `if`/`else` but `((void)0)` does not?*
- [ ] Answer: *what can a logging macro do that a variadic template cannot?*

Next: [09 — When a Macro Is the Right Answer](09-when-macros-are-correct.md).
