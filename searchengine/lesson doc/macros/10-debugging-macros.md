# 10 — Debugging Macros

> Macro bugs are hard for one reason: **the compiler reports on code you did not write.** The
> fix is always the same — stop reading the macro, and read what it expanded to.
>
> Four tools, and a decoder for the error messages.

---

## 1. `-E` — see the expansion

```bash
g++ -std=c++20 -E -P file.cpp | less
```

The single most useful command in the series. For a large file, narrow it:

```bash
# just the function you care about
g++ -std=c++20 -E -P file.cpp | sed -n '/int compute/,/^}/p'

# just your code, skipping the 50,000 lines of headers
g++ -std=c++20 -E file.cpp | awk '/yourfile\.cpp/,0'
```

Keep `-P` off when you need the `# 42 "file.cpp"` line markers to locate where output came from.

### Expand one macro without the headers

The trick for isolating a single macro: put it in a tiny file with no includes.

```bash
cat > /tmp/m.cpp <<'EOF'
#define SQUARE(x) x * x
SQUARE(n + 1)
EOF
g++ -E -P /tmp/m.cpp
```
```
n + 1 * n + 1
```

Two seconds, and the bug is visible. **Do this before reasoning about a macro**, not after.

---

## 2. `-dM` — see every definition

```bash
echo | g++ -std=c++20 -dM -E -x c++ -            # everything predefined (490 on your box)
g++ -std=c++20 -dM -E file.cpp | grep SEDB       # your project's macros after all includes
g++ -std=c++20 -dM -E file.cpp | grep -w PAGE_SIZE   # is this a macro at all?
```

That last one answers "why is my constant behaving strangely?" — if a name shows up in `-dM`
output, something `#define`d it, and doc 02 §2.1 explains what it will do to your code.

Related: `-dD` keeps the `#define` directives *alongside* the preprocessed output, so you can see
both the definition and the expansion in one file.

---

## 3. Decoding the error messages

The compiler always tells you, but the format is easy to misread.

### "in expansion of macro"

```
p8.cpp:1:18: error: expected unqualified-id before numeric constant
    1 | #define MAX_SIZE 100
      |                  ^~~
p8.cpp:2:21: note: in expansion of macro 'MAX_SIZE'
    2 | struct Widget { int MAX_SIZE; };
```

**Read it bottom-up.** The `note:` is the line *you* wrote; the `error:` points into the macro
definition. The error location is where the bad *token* came from; the note is where it was
*used*. The real bug is almost always at the note.

### "expected primary-expression before ')'"

```
error: expected primary-expression before ')' token
      | #define LOG_OLD(fmt, ...) printf(..., __VA_ARGS__)
      |                                                  ^
note: in expansion of macro 'LOG_OLD'
```

Classic empty `__VA_ARGS__` (doc 08 §2). The trailing comma has nothing after it.

### "'else' without a previous 'if'"

A multi-statement macro without `do { } while(0)` (doc 03 §4). The macro's second statement
terminated your `if`.

### An error pointing at a line that looks fine

The macro on that line expanded to something invalid. `-E` the file and look at the line.

### "macro 'X' passed 3 arguments, but takes just 2"

A comma inside a template argument (doc 03 §5). `std::map<int, std::string>` is two macro
arguments.

---

## 4. `#pragma message` and `#error` — printf debugging for the preprocessor

You cannot step through preprocessing, but you can make it talk.

```cpp
#define SEDB_STRINGIFY_(x) #x
#define SEDB_STRINGIFY(x)  SEDB_STRINGIFY_(x)

#pragma message("PAGE_SIZE is " SEDB_STRINGIFY(PAGE_SIZE))
#pragma message("compiling the Windows branch")
```

Prints at compile time. Use it to confirm which branch of an `#if` you are actually taking —
which is the question you will most often have.

```cpp
#if !defined(_WIN32) && !defined(__linux__)
  #error "Unsupported platform -- add a branch in DiskManager.cpp"
#endif
```

`#error` aborts with your message. **Put one in the `#else` of every platform check**, so an
unrecognised platform fails loudly at compile time instead of silently taking a wrong branch.
Your `DiskManager` currently assumes non-Windows means POSIX; an `#error` would make that
assumption explicit.

---

## 5. Finding out whether you took the branch

The most common conditional-compilation question, three ways:

```cpp
// 1. compile-time message
#if defined(_WIN32)
  #pragma message("Windows branch")
#else
  #pragma message("POSIX branch")
#endif

// 2. runtime evidence
const char* PlatformName() {
#if defined(_WIN32)
    return "windows";
#else
    return "posix";
#endif
}

// 3. inspect the output
// g++ -E -P DiskManager.cpp | grep -A3 "Seek"
```

Option 3 is definitive: it shows exactly what survived.

---

## 6. Checking the other branch compiles

The hazard from doc 04 §2 — the untaken branch is never parsed. Partial defences, in order of
strength:

```bash
# 1. CI on both platforms. The only real answer.

# 2. Force the other branch and at least check YOUR syntax
g++ -std=c++20 -U_WIN32 -fsyntax-only DiskManager.cpp
#    will fail on <unistd.h>, but catches your own typos first

# 3. Cross-compile if you have a toolchain
x86_64-linux-gnu-g++ -fsyntax-only DiskManager.cpp

# 4. Minimise the conditional region so there is less to rot (doc 04 section 2)
```

Number 4 is the one you control unilaterally, and it is why `PORTABLE_FSEEK` is a `#define`
rather than two implementations.

---

## 7. Tooling

**`clang -E`** often formats expansions more readably than GCC, and Clang's macro-expansion
diagnostics (`-fmacro-backtrace-limit=0`) show every level of a nested expansion — invaluable
for a macro built from three other macros.

**`clang-format`** does not reformat inside macros, which is why long macros drift into
unreadability. Keep them short.

**`gcc -save-temps`** writes the `.ii` (preprocessed) file alongside your object file, so you can
inspect exactly what a failing build fed the compiler:

```bash
g++ -std=c++20 -save-temps -c DiskManager.cpp    # leaves DiskManager.ii
```

**Compiler Explorer (godbolt.org)** has a "Preprocessor" output view — the fastest way to test a
macro idea in isolation.

---

## 8. The workflow

When a macro misbehaves:

1. **`-E -P` the file.** Look at the expansion. Most bugs are visible immediately.
2. **If the file is too big**, extract the macro into a 3-line scratch file and `-E` that.
3. **Read the error bottom-up** — the `note:` is your line, the `error:` is inside the macro.
4. **`-dM | grep NAME`** to check whether the name is a macro at all, and what it is defined to.
5. **`#pragma message`** to confirm which `#if` branch you are in.
6. **Ask whether it needs to be a macro** (doc 09 §6). Often the fastest fix is deletion.

---

## Checkpoint

- [ ] Reproduce the `int MAX_SIZE;` error and practise reading it bottom-up
- [ ] Add `#pragma message` to both branches of your `DiskManager` platform check and confirm
      which fires
- [ ] Add an `#error` to the `#else` of a platform check for an unsupported platform
- [ ] Run `-save-temps` on `DiskManager.cpp` and find `Seek` in the `.ii` file
- [ ] Use `-dM | grep -w PAGE_SIZE` to confirm it is **not** a macro in your build
- [ ] Force the non-Windows branch with `-U_WIN32 -fsyntax-only` and see how far it gets
- [ ] Answer: *why does a macro error point at the `#define` line rather than your code?*

Next: [11 — Real-World Patterns](11-real-world-patterns.md).
