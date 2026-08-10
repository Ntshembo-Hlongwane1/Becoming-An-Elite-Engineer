# 03 — Function-like Macros

> Macros that take arguments. They look like functions, they are not functions, and the gap
> between those two facts produces the most notorious bugs in C and C++.
>
> Four classic failures, all measured, all invisible in the source and obvious in the expansion.

---

## 1. What it is

```cpp
#define SQUARE(x) ((x) * (x))
```

*"Replace `SQUARE(` … `)` with the replacement text, substituting the argument tokens for `x`."*

The parenthesis must touch the name in the **definition**:

```cpp
#define F(x) ((x)+1)      // function-like: F(3) -> ((3)+1)
#define F (x) ((x)+1)     // object-like: F -> (x) ((x)+1)      <- a space changes everything
```

A space between `F` and `(` makes it an object-like macro whose replacement text happens to
start with `(x)`. This has cost people entire afternoons.

---

## 2. Failure 1 — operator precedence

```cpp
#define SQUARE_BAD(x) x * x
```

```cpp
int r1 = SQUARE_BAD(n + 1);
```

Expansion:

```cpp
int r1 = n + 1 * n + 1;
```

Measured:

```
n = 4
  SQUARE_BAD(n+1) = 9   (expected 25)
```

`4 + 1*4 + 1` = 9. The argument was pasted in as *text*, and `*` binds tighter than `+`. A
function would have evaluated `n + 1` first, because a function receives a **value**; a macro
receives **tokens**.

**Fix: parenthesise every parameter, and the whole body.**

```cpp
#define SQUARE_OK(x) ((x) * (x))
```

Both are required, and for different reasons:

- **Inner parens** protect the argument from the macro's own operators — the bug above.
- **Outer parens** protect the macro's result from the *surrounding* expression:
  ```cpp
  #define ADD(a,b) (a) + (b)
  int x = 10 * ADD(1,2);        //  10 * (1) + (2)  ==  12, not 30
  ```

---

## 3. Failure 2 — multiple evaluation

This one cannot be fixed with parentheses.

```cpp
#define MAX(a,b) ((a) > (b) ? (a) : (b))
int a = 3, b = 4;
int r = MAX(a++, b++);
```

Expansion:

```cpp
int r = ((a++) > (b++) ? (a++) : (b++));
```

Measured:

```
a=3 b=4
  MAX(a++,b++) = 5  -> a=4 b=6   (b incremented TWICE)
  maxFn(a++,b++) = 4 -> a=4 b=5      <- the function template
```

`b` was incremented twice, because `b++` appears twice in the expansion and the winning branch
evaluates it again. The result 5 is neither `max(3,4)` nor `max(4,5)`.

GCC did warn here:

```
warning: operation on 'a' may be undefined [-Wsequence-point]
note: in definition of macro 'MAX'
```

but only because the argument had an obvious side effect. `MAX(expensive(), other())` calls
`expensive()` twice with no warning at all — a silent performance bug — and
`MAX(*it++, *jt++)` corrupts iterators.

**There is no macro-level fix.** Any argument used more than once in the body is a hazard. The
fix is to not use a macro:

```cpp
template <typename T> constexpr T maxFn(T a, T b) { return a > b ? a : b; }
```

A function evaluates its arguments exactly once, before the body runs. That is a property macros
structurally cannot have.

---

## 4. Failure 3 — multiple statements and the `if` trap

```cpp
#define LOG_BAD(m) printf("log: %s\n", m); printf("(second)\n")
```

```cpp
if (flag) LOG_BAD("x");
```

Expands to:

```cpp
if (flag) printf("log: %s\n", "x"); printf("(second)\n");
```

The second `printf` is **outside the `if`** and runs unconditionally. Add an `else` and it does
not even compile — `error: 'else' without a previous 'if'`.

### The naive fix fails too

```cpp
#define LOG_BRACES(m) { printf(...); printf(...); }

if (flag) LOG_BRACES("x"); else foo();
//        { ... } ;        else       <- the stray semicolon terminates the if
```

Braces plus the caller's `;` produce an empty statement between the block and the `else`.

### The idiom: `do { ... } while(0)`

```cpp
#define LOG_OK(m)  do { printf("log: %s\n", m); printf("(second)\n"); } while(0)
```

Verified:

```
if (flag) LOG_OK("flagged"); else printf("  LOG_OK: else branch reached correctly\n");
```
```
  LOG_OK: else branch reached correctly
```

Why it works:

- `do { } while(0)` is a **single statement**, so it fits anywhere a statement is expected.
- It *requires* a trailing semicolon, so `LOG_OK(x);` reads naturally and `LOG_OK(x)` without one
  is an error — consistent with function calls.
- The loop runs exactly once, and every compiler eliminates it entirely.

**Any multi-statement macro must use `do { } while(0)`.** No exceptions. If you see a macro with
braces and no `do/while`, it is a latent bug.

> `while(0)` may draw a "conditional expression is constant" warning from MSVC; the standard
> silencer is `while(0,0)` or `__pragma(warning(suppress:4127))`. GCC and Clang are quiet.

---

## 5. Failure 4 — commas in arguments

```cpp
#define DECLARE(type, name) type name
DECLARE(std::map<int, std::string>, m);       // ERROR
```

The preprocessor splits arguments on commas **without understanding templates**. It sees three
arguments: `std::map<int`, `std::string>`, and `m`. The angle brackets mean nothing to it.

Workarounds, all unpleasant:

```cpp
using IntStrMap = std::map<int, std::string>;     // 1. alias it first (best)
DECLARE((std::map<int, std::string>), m);         // 2. extra parens -- needs an unwrap macro
#define COMMA ,
DECLARE(std::map<int COMMA std::string>, m);      // 3. genuinely used in real code
```

That third one exists in production codebases and is a fair summary of where macro metaprogramming
ends up.

---

## 6. The replacements

| Macro use | Replacement | Why better |
|---|---|---|
| `#define SQUARE(x) ((x)*(x))` | `constexpr` function | single evaluation, typed, debuggable |
| `#define MAX(a,b) ...` | function template | same, plus `std::max` already exists |
| `#define SWAP(a,b) ...` | `std::swap` | ADL, move-aware (rule-of-five doc 08 §4) |
| Type-generic code | template | actual type checking |
| Speed | `inline` / the optimiser | the compiler inlines better than you do |

**"Macros are faster" has been false for decades.** A `constexpr` or `inline` function is
inlined by any optimising compiler, and the compiler can also *decline* when inlining would hurt
the instruction cache — a judgement a macro cannot make. `lesson doc/storage/12-latency-lab.md`
§8.1 covers this.

### What survives

Function-like macros remain the right tool in exactly two situations:

1. **You need `__FILE__` / `__LINE__` from the caller** — doc 06 §3. A function sees its own
   location, not its caller's.
2. **You need the argument's *text*, not its value** — `#x` stringification, doc 07. `assert`
   printing the failed expression is the canonical case.

Both are doc 09's "legitimate uses" list, and both are things the language genuinely cannot
express otherwise. (C++20's `std::source_location` retires the first.)

---

## 7. If you must write one

- [ ] Parenthesise **every parameter** and the **whole body**
- [ ] Use each parameter **exactly once**, or document loudly that arguments must be side-effect free
- [ ] Wrap multi-statement bodies in `do { } while(0)`
- [ ] `SCREAMING_SNAKE_CASE` with a project prefix
- [ ] No `return`, `break`, or `continue` inside — they escape into the caller's control flow
- [ ] Check the expansion with `g++ -E` before trusting it
- [ ] Write a comment saying **why a function would not do**

That last one is the real test. If you cannot state why a function is impossible, use a function.

---

## Checkpoint

- [ ] Reproduce all four failures and view each expansion with `-E -P`
- [ ] Fix `SQUARE_BAD` and confirm the expansion changes
- [ ] Write `MAX(expensive(), other())` where `expensive()` prints; count the calls
- [ ] Reproduce the `else` compile error with a braces-but-no-`do/while` macro
- [ ] Try `DECLARE(std::map<int, std::string>, m)` and read the error
- [ ] Convert one function-like macro in code you have read into a `constexpr` function
- [ ] Answer: *why can parentheses fix failure 1 but not failure 2?*
- [ ] Answer: *why `do { } while(0)` rather than plain braces?*

Next: [04 — Conditional Compilation](04-conditional-compilation.md) — the one job only macros
can do, and the one your `DiskManager` depends on.
