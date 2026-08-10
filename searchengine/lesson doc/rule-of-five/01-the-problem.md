# 01 — The Problem: Resources and Ownership

> **The claim this doc establishes:** the five special member functions are not a style rule, a
> convention, or a C++ quirk. They are the *only* mechanism the language gives you to answer a
> question that has no default correct answer: **when this object is copied or destroyed, what
> happens to the thing it owns?**
>
> We will write a class that gets it wrong, run it, and watch Windows kill the process with
> `0xC0000374`.

---

## 1. What "owning a resource" means

A **resource** is anything acquired from outside your object that must eventually be given
back:

| Resource | Acquired by | Must be released by |
|---|---|---|
| Heap memory | `malloc` / `new` | `free` / `delete` |
| A file handle | `fopen` / `open` | `fclose` / `close` |
| A buffer pool pin | `FetchPage` | `UnpinPage` |
| A mutex | `lock` | `unlock` |
| A socket, a GPU buffer, a database transaction | … | … |

The defining property is **exclusivity of responsibility**: exactly one entity must do the
releasing, exactly once. Not zero times (a leak). Not twice (corruption).

An `int` is not a resource. Copying an `int` produces a second, independent `int`, and nothing
needs releasing. **This distinction is the entire subject of the series.** Types that own no
resources need none of the five special members — that is the Rule of Zero, and it is the
majority of types you will write.

---

## 2. The class that gets it wrong

```cpp
struct BrokenString {
    char* data;

    BrokenString(const char* s) {
        data = (char*)malloc(strlen(s) + 1);
        strcpy(data, s);
    }
    ~BrokenString() {
        printf("  dtor freeing %p\n", (void*)data);
        free(data);
    }
};
```

This looks complete and responsible. It acquires in the constructor and releases in the
destructor — the textbook shape. And for a program that never copies a `BrokenString`, it is
perfectly correct.

**But we never said what a copy means.** So the compiler decided for us, and its decision is
documented, deterministic, and wrong for this class: it generates a copy constructor that
copies each member. `data` is a `char*`. Copying a `char*` copies **the address**, not what it
points at.

```cpp
BrokenString a("hello");
BrokenString b = a;        // b.data and a.data are now the SAME pointer
```

Two objects. One buffer. Two destructors that will both `free` it.

---

## 3. Running it

```cpp
int main(){
    setvbuf(stdout, nullptr, _IONBF, 0);      // unbuffered, so output survives the crash
    printf("create a, then copy to b:\n");
    BrokenString a("hello");
    printf("  a.data = %p\n", (void*)a.data);
    {
        BrokenString b = a;
        printf("  b.data = %p   <-- SAME POINTER (shallow copy)\n", (void*)b.data);
        printf("  b leaving scope...\n");
    }
    printf("  a.data now dangles: %p\n", (void*)a.data);
    printf("  a's dtor will free it AGAIN ->\n");
    return 0;
}
```

Actual output on your toolchain:

```
create a, then copy to b:
  a.data = 000001aee8869630
  b.data = 000001aee8869630   <-- SAME POINTER (shallow copy)
  b leaving scope...
  dtor freeing 000001aee8869630
  a.data now dangles: 000001aee8869630
  a's dtor will free it AGAIN ->
  dtor freeing 000001aee8869630

---- exit: -1073740940 (0xC0000374) ----
```

`0xC0000374` is `STATUS_HEAP_CORRUPTION`. The Windows heap detected the second `free` of an
already-free block and terminated the process immediately. On Linux you would get
`free(): double free detected in tcache 2` and `SIGABRT`.

### Read the trace carefully — there are three separate bugs, not one

**Bug 1 — the double free.** Both destructors ran on the same address. That is the crash.

**Bug 2 — the dangling pointer.** Look at the line *before* the crash: after `b` was destroyed,
`a.data` still held `000001aee8869630`, which is now freed memory. Between that moment and the
crash, **`a` was a live object whose every method would read freed memory.** If the program had
used `a` there, it would have read whatever the allocator had since put in that block. No crash,
just wrong data.

**Bug 3 — the one you cannot see.** Had we written `a = b` (assignment rather than
construction), the compiler-generated copy *assignment* would have overwritten `a.data` with
`b.data` — **leaking** the buffer `a` used to own, since nothing now points at it. A leak, a
double-free, and a dangling pointer, all from the same omission.

> **The bug is not in any line you can point at.** It is in a line that *isn't there*. The
> compiler wrote the copy constructor, and it wrote a reasonable one — for a struct of `int`s.
> The mismatch is between what the compiler assumed and what `data` means to you.

---

## 4. Why the compiler's default is memberwise copy

This is worth defending rather than resenting, because the default is right far more often than
it is wrong.

```cpp
struct Point { double x, y; };
struct Rect  { Point topLeft, bottomRight; };
```

Memberwise copy is exactly correct here, and for the overwhelming majority of types. If the
compiler demanded you hand-write a copy constructor for every struct, C++ would be unusable.

So the language's position is: **memberwise copy is the default; if your type owns something,
say so.** The five special members are how you say so.

The awkwardness is that `char*` is ambiguous by design. Is it:

- a **pointer to memory I own** (needs deep copy + free), or
- a **pointer to memory someone else owns** (needs shallow copy, no free), or
- a **pointer into the middle of a buffer** (an iterator, copy freely)?

The type `char*` cannot express which. Only your class's behaviour can — which is precisely
what declaring the special members does. **The special members are where you tell the compiler
what your pointers mean.**

---

## 5. The three ways out

Every solution to this problem is one of three, and each is a doc in this series.

### Option A — forbid copying

```cpp
BrokenString(const BrokenString&)            = delete;
BrokenString& operator=(const BrokenString&) = delete;
```

The double-free becomes a **compile error**. This is what `DiskManager` does (it owns a
`FILE*`) and what `BufferPool` does. It is the right answer whenever copying is meaningless,
and it costs nothing.

Doc 04 §6 covers when this is right and how it interacts with the rest of the five.

### Option B — define what a copy means (deep copy)

```cpp
BrokenString(const BrokenString& o) {
    data = (char*)malloc(strlen(o.data) + 1);
    strcpy(data, o.data);                       // a SECOND buffer
}
```

Now each object owns its own buffer and each destructor frees its own. Correct, and expensive —
copying a 1 MB string allocates and copies 1 MB.

Doc 04 builds this properly, including the assignment operator and the self-assignment trap
that catches nearly everyone.

### Option C — transfer ownership instead of duplicating (move)

```cpp
BrokenString(BrokenString&& o) noexcept : data(o.data) {
    o.data = nullptr;                           // source no longer owns it
}
```

No allocation, no copying — one pointer assignment. The source is left owning nothing, so its
destructor has nothing to free (`free(nullptr)` is defined and does nothing).

This is **move semantics**, doc 05, and it is why the Rule of Three became the Rule of Five in
C++11.

---

## 6. Ownership models, and which one your class is

Before writing any special members, answer this. Everything else follows from it.

| Model | Copy means | Example |
|---|---|---|
| **Value** (owns nothing indirectly) | memberwise; trivial | `Point`, `Page`, `PostingRef` |
| **Unique ownership** | *forbidden*; move only | `unique_ptr`, `PageGuard`, `DiskManager` |
| **Deep / value-like ownership** | duplicate the resource | `std::string`, `std::vector` |
| **Shared ownership** | share, and count | `shared_ptr` |
| **Non-owning view** | copy the reference; never release | `string_view`, `span`, `NodePage` |

Your `NodePage` from `storage/05` is the last row: it holds a `Page&` and owns nothing. It
needs no special members at all — and doc 11 §4 examines why that is the correct design rather
than an oversight.

Your `PageGuard` is the second row. Your `DiskManager` is the second row. Your in-memory
`BPlusTree` is the second row (it owns raw node pointers). Three classes, one model, and doc 11
audits all three.

> **Pick the model first.** Nearly every mistake in this area comes from writing special members
> before deciding what the type *is*. Once you know it is unique-ownership, the five write
> themselves: delete both copies, implement both moves, implement the destructor.

---

## 7. Why this is worse than it looks

The `BrokenString` crash was loud and immediate — the best possible outcome. Real instances of
this bug are not:

- **The crash is elsewhere.** The heap detects corruption at the *next* allocation, which may be
  in unrelated code, thousands of operations later.
- **It may not crash at all.** Bug 2 above — reading freed memory — usually returns plausible
  data, because the allocator has not yet reused the block. It fails only under load, or on
  another machine, or after an unrelated change shifts allocation timing.
- **No warning fires.** I compiled with `-Wall -Wextra`. Nothing. The compiler cannot know that
  `data` is owned.

The one tool that *does* catch it is a sanitizer:

```bash
g++ -std=c++20 -g -fsanitize=address prog.cpp -o prog     # ASan: double-free, use-after-free
```

ASan reports the exact free, the exact previous free, and both stack traces. Your MinGW build
did not ship `libasan` (the `-fsanitize=undefined` link failed earlier for the same reason), so
on this toolchain you are relying on the Windows heap's own checks, which are much coarser.
That is worth knowing about your environment: **you have weaker automatic detection than a
Linux developer, so the discipline has to be stronger.**

---

## Checkpoint

Build and run `BrokenString` yourself. Do not skip this — the rest of the series is a response
to it, and reading a crash transcript is not the same as producing one.

- [ ] Reproduce the double free, and record the exit code your machine reports
- [ ] Add `printf` to the copy constructor. Observe that **it never fires** — the compiler's
      generated one runs, and it is invisible
- [ ] Change `BrokenString b = a;` to `a = b;` and observe the *leak* variant (no crash; run it
      in a loop and watch memory grow in Task Manager)
- [ ] Apply Option A (`= delete`) and confirm the program no longer compiles — note *which*
      line the error points at
- [ ] Answer: *why is memberwise copy the right default for the language, even though it is
      wrong for this class?*
- [ ] Answer: *which of the five ownership models in §6 does your `LeafNode` belong to?*

Next: [02 — Object Lifetime & the Destructor](02-lifetime-and-destructor.md), which establishes
exactly when destructors run — because "both destructors ran" is the sentence that killed the
program above, and you need to know precisely when that happens.
