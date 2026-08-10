# 01 — What the Preprocessor Is

> **The claim this doc establishes:** the preprocessor is a separate language, processed by a
> separate pass, that produces a *new source file* which is what the C++ compiler actually reads.
> Once you internalise that, every macro bug becomes obvious rather than mysterious.
>
> **The habit it teaches:** `g++ -E`. Look at the text, not the macro.

---

## 1. Translation phases — where the preprocessor sits

The standard defines compilation as a sequence of **translation phases**. Simplified to the ones
that matter:

```
   your .cpp file
        |
   1-3. physical characters -> tokens; comments become whitespace
        |
   4.   PREPROCESSOR: #include, #define, #if, macro expansion
        |
        v
   ---- a single, flat "translation unit" of pure C++ -----
        |
   7.   COMPILER: parsing, types, templates, overload resolution, codegen
        |
   9.   LINKER
```

Two facts fall out of the diagram, and they explain almost everything:

**The preprocessor finishes before the compiler starts.** By the time any C++ rule is applied,
every `#include` has been pasted in, every `#if` has been resolved and the losing branches
*deleted*, and every macro has been expanded. The compiler never sees a `#define`.

**Comments are gone before macros are expanded** (phase 3 vs phase 4). So a macro can never
contain a comment in a meaningful way, and `// ...` at the end of a multi-line macro silently
eats the backslash-continuation.

---

## 2. Seeing it

This is the single most useful command in the series.

```bash
g++ -std=c++20 -E -P yourfile.cpp
```

- `-E` — stop after preprocessing, print the result
- `-P` — omit the `#line` markers, which are noise for reading

Given:

```cpp
#define SQUARE_BAD(x)  x * x
#define SQUARE_OK(x)  ((x) * (x))
#define MAX(a,b)  ((a) > (b) ? (a) : (b))
int compute(int n){
    int r1 = SQUARE_BAD(n + 1);
    int r2 = SQUARE_OK(n + 1);
    int a = 3, b = 4;
    int r3 = MAX(a++, b++);
    return r1 + r2 + r3;
}
```

The compiler receives:

```cpp
int compute(int n){
    int r1 = n + 1 * n + 1;
    int r2 = ((n + 1) * (n + 1));
    int a = 3, b = 4;
    int r3 = ((a++) > (b++) ? (a++) : (b++));
    return r1 + r2 + r3;
}
```

**The `#define` lines are gone. The macro names are gone.** There is no trace that a macro was
ever involved. This is why:

- Your debugger cannot step into a macro — there is no function to step into.
- Your IDE cannot "go to definition" reliably.
- Error messages point at expanded text you never typed.
- A symbolic debugger shows the *line* the macro was used on, with code that is not there.

Look at `r1` and `r3`. Both bugs are visible in the expansion and invisible in the source. Doc 03
is about both.

---

## 3. What the preprocessor actually understands

Its entire vocabulary:

| Directive | Purpose |
|---|---|
| `#include` | paste another file here |
| `#define` / `#undef` | create / destroy a macro |
| `#if` `#ifdef` `#ifndef` `#elif` `#else` `#endif` | keep or delete a region of text |
| `#error` / `#warning` | abort or complain at preprocessing time |
| `#pragma` | implementation-specific instruction |
| `#line` | override the reported line number |
| `defined(X)` | operator usable only in `#if` |
| `#` `##` | stringify, token-paste (doc 07) |

That is all of it. Notice what is absent: **no types, no scopes, no namespaces, no functions, no
classes, no templates, no `const`.** The preprocessor cannot know that `x` is an `int` or that
you are inside a class. It manipulates tokens.

### It does understand integer arithmetic — in `#if` only

```cpp
#if 1 + 2 * 3 == 7
    // this region is kept
#endif
```

`#if` evaluates a restricted integer constant expression: arithmetic, comparison, `&&`, `||`,
`defined()`. It cannot use `sizeof`, floating point, or any C++ entity. `#if sizeof(int) == 4`
is an error, which surprises people constantly — `sizeof` is a compiler concept and the compiler
has not run yet.

---

## 4. `#include` is literally a paste

```cpp
#include "Page.hpp"
```

means *"replace this line with the entire contents of Page.hpp, after preprocessing that file
too."* Recursively.

Which is why a real translation unit is enormous. Your `DiskManager.cpp` is 150 lines; after
preprocessing it is tens of thousands, because `<cstdio>`, `<string>`, `<stdexcept>` and their
transitive includes are all pasted in. Check yours:

```bash
g++ -std=c++20 -E internal/kernal/core/storage/DiskManager.cpp | wc -l
```

Doc 05 is about the consequences: include guards, build times, and why `#include` order can
change behaviour.

---

## 5. The mental model: a program that writes a program

The clearest way to hold this:

> **The preprocessor is a code generator whose output you never see unless you ask.**

That framing gets three things right at once:

- **Its errors are generator errors.** A bad macro produces bad *generated code*, and the
  compiler complains about the generated code. That is why the message so often makes no sense
  against your source — the compiler is describing a file you did not write. Doc 10 §3 decodes
  the standard forms.
- **It is Turing-incomplete but startlingly capable.** No loops or recursion (macros cannot
  expand recursively), yet doc 07's X-macros and doc 08's variadic tricks generate substantial
  code. Whole libraries — Boost.Preprocessor — exist to push this further, and the results are
  formidable and unreadable.
- **Generated code needs the same review as written code.** If you cannot predict a macro's
  expansion, you cannot review its use.

---

## 6. Why it still exists

C++ has spent thirty years replacing macros — `const` and `constexpr`, `inline`, templates,
`enum class`, `constexpr if`, `std::source_location`, modules. Yet the preprocessor is still
mandatory, for one structural reason:

> **It is the only thing that runs early enough to *delete* code before it is parsed.**

Everything the compiler offers happens *after* parsing, so everything must be valid C++ first.
`if constexpr` still requires both branches to parse. Only `#if` can remove text containing
`<unistd.h>` on a machine where that header does not exist.

That is doc 04's subject and the one job with no replacement. Every other macro use in your code
should be under suspicion.

---

## Checkpoint

- [ ] Run `g++ -E -P` on a file of your own; read the output
- [ ] Run it on `DiskManager.cpp` and count the lines. Compare to the source
- [ ] Reproduce the `SQUARE_BAD` / `MAX` expansion above and confirm both bugs are visible
- [ ] Try `#if sizeof(int) == 4` and read the error; explain why it cannot work
- [ ] Put a `// comment` at the end of a line ending in `\` inside a multi-line macro. Watch it
      break, and explain why using phase ordering
- [ ] Answer: *why can't a debugger step into a macro?*
- [ ] Answer: *why is `#if` the only tool that can guard a platform-specific `#include`?*

Next: [02 — Object-like Macros](02-object-like-macros.md).
