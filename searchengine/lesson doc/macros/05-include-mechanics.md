# 05 — `#include` Mechanics

> `#include` is the directive you use most and think about least. It is a **textual paste**, and
> every property that follows — build times, include guards, order dependence, the reason
> `<windows.h>` breaks your code — comes from that.
>
> Measured on your repo: `BPlusTree.hpp` is 140 lines and expands to **50,166**.

---

## 1. It is a paste, and the numbers are large

```bash
$ g++ -std=c++20 -E file.cpp | wc -l
```

| What you wrote | What the compiler reads |
|---|---|
| `#include <cstdio>` | 1,823 lines |
| `#include <vector>` | 27,954 lines |
| `#include <string>` | 33,220 lines |
| `#include <iostream>` | 44,131 lines |
| **your `BPlusTree.hpp` + a `main`** | **50,166 lines** |

Your B+Tree header is 140 lines of your own code. The compiler parses fifty thousand, **for
every `.cpp` that includes it**, on every build.

That is the cost model behind three practical rules in §5. It is also why C++20 modules exist —
a module is parsed once, not once per includer.

> Your `BPlusTree.hpp` pulls `<iostream>` for `Print()`. That single include is ~44k of the 50k.
> Moving `Print` to a separate header, or switching to `<ostream>` (declarations only, no global
> stream objects), would cut the header's cost by most of that. A real, measurable win if
> compile time ever bothers you.

---

## 2. The two forms

```cpp
#include <vector>       // angle brackets
#include "Page.hpp"     // quotes
```

The standard says only that `""` may search "an implementation-defined manner" first and then
falls back to the `<>` path. In practice, every compiler:

- **`"..."`** — search the directory of the *including file* first, then the `-I` paths.
- **`<...>`** — search only the `-I` paths and the system directories.

**Convention: `<>` for system and third-party headers, `""` for your own.** Your storage headers
use `"Page.hpp"` for siblings and `<cstdio>` for the standard library — correct.

Note `#include "..."` resolving relative to the *including file*, not the current directory, is
what makes `#include "../../storage/BufferPool.hpp"` work from
`datastructures/bplustree/DiskBPlusTree.hpp`.

---

## 3. Include guards

Without protection, a header included twice defines everything twice — an error for classes.

```cpp
// A.hpp includes Page.hpp
// B.hpp includes Page.hpp
// main.cpp includes both  ->  struct Page defined twice
```

### The two mechanisms

```cpp
#ifndef SEARCHENGINE_STORAGE_PAGE_HPP     // traditional
#define SEARCHENGINE_STORAGE_PAGE_HPP
...
#endif
```

```cpp
#pragma once                               // what your code uses
```

| | `#ifndef` guard | `#pragma once` |
|---|---|---|
| Standard | **yes** | no — but universally supported |
| Speed | must open and scan the file | compiler skips by identity |
| Name collisions | possible if you pick a weak name | impossible |
| Same file via two paths | still guarded correctly | **may include twice** |

**The `#pragma once` failure mode** is the only real argument against it: if a file is reachable
by two different paths — a symlink, a hard link, a directory included twice under different
names — the compiler may treat them as different files. Rare on a normal project layout; real in
large monorepos.

`storage/02` §1 chose `#pragma once`, and for this repo that is correct: fewer lines, faster,
and no risk of a duplicated guard name.

### If you write guards, name them properly

```cpp
#ifndef PAGE_HPP                            // BAD: will collide
#ifndef SEARCHENGINE_STORAGE_PAGE_HPP       // GOOD: project + path + file
#ifndef _PAGE_HPP_                          // ILLEGAL: leading underscore is reserved
```

Identifiers starting with `_` followed by a capital, or containing `__`, are **reserved to the
implementation** (doc 02 §5). `_PAGE_HPP_` is technically undefined behaviour, and it is
everywhere in older code.

---

## 4. Include order can change behaviour

Because it is a paste, order matters more than it should.

```cpp
#include <windows.h>       // #defines min, max, near, far, small...
#include <algorithm>       // std::min is now broken
```

versus

```cpp
#define NOMINMAX           // ask windows.h not to
#include <windows.h>
#include <algorithm>
```

Any header that defines macros can alter the meaning of everything included after it. This is
doc 02 §2.1's "a macro in a header poisons every file downstream," and it is why platform SDK
headers should be included **last**, or isolated in a single `.cpp`.

### Self-contained headers

> **Every header must compile on its own**, including whatever it needs.

If `NodePage.hpp` uses `std::memcpy` but relies on whoever includes it having already included
`<cstring>`, it works until someone includes it first — and then breaks in a file that did
nothing wrong.

The test is mechanical, and worth adding to your build:

```bash
for h in internal/kernal/core/storage/*.hpp; do
    echo "#include \"$h\"" > /tmp/t.cpp && echo "int main(){}" >> /tmp/t.cpp
    g++ -std=c++20 -fsyntax-only -I. /tmp/t.cpp || echo "NOT SELF-CONTAINED: $h"
done
```

**Corollary: include what you use.** If a file names `std::vector`, it includes `<vector>` —
even if some other header already did. Relying on a transitive include means an unrelated
refactor breaks you.

---

## 5. Reducing the 50,166

Three techniques, in order of payoff.

### Forward declare instead of including

```cpp
// BufferPool.hpp
class DiskManager;                 // enough for a reference or pointer member

class BufferPool {
    DiskManager& m_Disk;           // does NOT need the full definition
};
```

A declaration suffices for references, pointers, and function parameters/returns. You need the
full definition only to: create an object, access a member, call a method, inherit from it, or
take its `sizeof`.

`BufferPool.hpp` currently includes `DiskManager.hpp` for a reference member. Forward-declaring
it and including the real header in `BufferPool.cpp` removes `<cstdio>`, `<string>`, and
`<stdexcept>` from everything that includes `BufferPool.hpp`.

### Move implementation to the `.cpp`

Anything not a template can leave the header. This is exactly why your `DiskManager` has a
`.cpp` and your `BPlusTree` does not — templates must be visible at instantiation.

### Prefer the narrower header

`<ostream>` instead of `<iostream>` when you only need `std::ostream&` and not `std::cout`.
`<iosfwd>` if you only need the *declaration*. For your `BPlusTree::Print(std::ostream&)`, the
default argument `= std::cout` is what forces the heavy include — dropping the default and
having callers pass the stream would let you use `<iosfwd>`.

---

## 6. What `#include` cannot do

- **No conditional include of a name computed at runtime.** Everything is resolved in phase 4.
- **No "include once globally."** Guards are per-translation-unit; every `.cpp` re-parses
  everything.
- **Circular includes do not work.** `A.hpp` including `B.hpp` including `A.hpp` is stopped by
  the guard, which means one of them sees an *incomplete* `A`. The fix is a forward declaration,
  which is one more reason to prefer them.

C++20 **modules** address all three, and `import std;` replaces the 50,166 lines with a parsed
binary artifact. Toolchain support is still uneven — GCC 16 has partial support — so this series
stays with headers, but that is the direction.

---

## Checkpoint

- [ ] Run `g++ -E ... | wc -l` on three of your own headers. Note the worst offender
- [ ] Confirm `<iostream>` is most of `BPlusTree.hpp`'s cost; try `<ostream>` and re-measure
- [ ] Run the self-contained test loop over `internal/kernal/core/storage/`
- [ ] Forward-declare `DiskManager` in `BufferPool.hpp` and measure the drop
- [ ] Create a deliberate circular include and read the error; fix it with a forward declaration
- [ ] Rename a `#pragma once` header to use an `#ifndef` guard; pick a name that would collide,
      and observe what happens
- [ ] Answer: *when is a forward declaration insufficient?*
- [ ] Answer: *why must every header be self-contained?*

Next: [06 — Predefined Macros](06-predefined-macros.md) — the 490 you already have.
