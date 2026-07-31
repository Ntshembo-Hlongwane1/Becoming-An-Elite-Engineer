# Subsystem Identity, Part 1: Enum as Key, String as Label

> **Goal:** Replace the string keys in `Kernal` with a scoped enum, so that asking for a
> subsystem that doesn't exist becomes a **compile error** instead of a `nullptr` that
> crashes three subsystems later.
>
> **Scope:** This document changes *how the Kernal identifies subsystems*. It does not change
> what any subsystem does, does not change startup order, and does not change a single line of
> printed output. If your program behaves differently after this refactor, you made a mistake —
> and Section 13 tells you exactly which one.

---

## 0. The mental model (read this before touching a file)

Right now your codebase uses the word "name" for **two different jobs**, and they are not the
same job:

| | **Identity key** | **Display label** |
|---|---|---|
| **Question it answers** | "Which subsystem do you mean?" | "What do I print in the log?" |
| **Who reads it** | The compiler / the map | A human |
| **How many valid values?** | Exactly 5 — a closed set | Infinite — any string |
| **Cost of a typo** | Silent `nullptr` → crash later | Ugly log line |
| **Where it lives now** | `main.cpp:26` `"Store"`, `kernal.cpp:63` `"Store"` | `Subsystem::Name()` — `store.cpp:4` |

Both are currently `std::string`. Because they are the same *type*, nothing stops you from
using one where the other belongs, and nothing stops them drifting apart.

The fix is not "put the strings in one file." The fix is: **give identity its own type.**

> **The core principle:** when a value has a *closed set* of valid options known at compile
> time, it should be a type the compiler can check — not a string. `std::string` is the right
> type for data that came from a user or a file. It is the wrong type for a choice you made
> while writing the program.

After this refactor:

- **Identity** is `SubsystemId` — an enum. The compiler rejects anything that isn't one of five values.
- **Label** is `std::string_view` — derived *from* the id by a single function.

Because the label is derived from the id, they physically cannot drift apart.

### Why this is worth doing on *your* code specifically

Look at this sequence in `cmd/main.cpp`:

```cpp
kernal.Register("Store", new Store());                                  // line 26
Store* store = dynamic_cast<Store*>(kernal.GetSubsystem("Store"));      // line 38
kernal.Register("Search Engine", new Engine(store));                    // line 39
```

Misspell line 38 as `"Stroe"`. It compiles. It links. It runs. `GetSubsystem` returns
`nullptr` (`kernal.cpp:126`). `Engine` is constructed holding a null `Store*`
(`engine.hpp:10`). Nothing complains. You crash later, inside `Engine::Run()`, on a
thread, with a stack trace that points nowhere near line 38.

After this refactor, `SubsystemId::Stroe` fails to compile with an error that names the exact
line. That is the entire point.

---

## 1. Pre-flight inventory

Before changing anything, know every place a subsystem name appears. There are **exactly 14**.
This is your checklist — nothing outside this list needs to change.

### `internal/kernal/kernal.hpp`
| Line | Current | Why it's here |
|---|---|---|
| 28 | `Error Register(const std::string& name, Subsystem*)` | key parameter |
| 36 | `Subsystem* GetSubsystem(const std::string& name) const` | key parameter |
| 40 | `std::map<std::string, std::unique_ptr<Subsystem>> subsystems_` | key type |
| 41 | `std::vector<std::string> order_` | key type |
| 45 | `Error StartSubSystem_(std::string& name)` | key parameter |

### `internal/kernal/kernal.cpp`
| Line | Current | Why it's here |
|---|---|---|
| 6–14 | destructor loop over `order_` | iterates keys, prints `*it` |
| 25–36 | `Register` body | inserts key, prints key |
| 41–52 | `InitAll` loop | iterates keys, prints key |
| 63 | `GetSubsystem("Store")` | **lookup by literal** |
| 100–112 | `StopAll` loop | iterates keys, prints key |
| 121–127 | `GetSubsystem` body | `.find()` on key |
| 129–132 | `StartSubSystem_` body | `operator[]` on key |

### `cmd/main.cpp`
| Line | Current |
|---|---|
| 26, 29, 32, 35, 39 | five `Register("...", ...)` calls |
| 38 | `GetSubsystem("Store")` — **lookup by literal** |

### What is **not** on this list (do not touch)
- `Subsystem::Name()` in `subsystem.hpp:18` and all five overrides. These are **labels**, and
  labels are staying as strings. That's correct — that's the whole design.
- `Kernal::GetName()` (`kernal.cpp:17`). The Kernal is not registered in its own map, so it has
  no id. It only has a label. Leave it alone.

---

## 2. The invariant you are creating

Write this down, because every step below serves it:

> **There is exactly one place in the codebase where the string `"Store"` appears.**

Today it appears in three places (`main.cpp:26`, `kernal.cpp:63`, `store.cpp:4`). When you're
done it will appear in one: the `ToString` function. Everything else refers to
`SubsystemId::Store`.

---

## 3. Step 1 — Create the identity type

**New file:** `internal/kernal/core/headerfiles/subsystemid.hpp`

This location matters: it sits next to `subsystem.hpp` and `error.hpp`, which are the
Kernal's core vocabulary types. `SubsystemId` is now part of that vocabulary.

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// The closed set of subsystems this program can contain.
//
// This is the IDENTITY of a subsystem, not its label. Adding a subsystem to the
// program means adding a value here first.
//
// NOTE: The order of these values does NOT determine startup order. Startup order
// is registration order, tracked by Kernal::order_. See Section 9.
enum class SubsystemId : std::uint8_t {
    Store,
    DirReader,
    Lexer,
    Parser,
    Engine,
};

// The DISPLAY LABEL for an id. This is the single source of truth for
// subsystem naming in the entire program.
//
// `inline` is mandatory: without it, every .cpp that includes this header emits
// its own definition and the linker fails with "multiple definition". See Section 13.
inline std::string_view ToString(SubsystemId id) {
    switch (id) {
        case SubsystemId::Store:     return "Store";
        case SubsystemId::DirReader: return "Dir Reader";
        case SubsystemId::Lexer:     return "Lexer";
        case SubsystemId::Parser:    return "Parser";
        case SubsystemId::Engine:    return "Search Engine";
    }
    return "UnknownSubsystem";
}

// Convenience for building error messages with operator+.
// You cannot write `"[" + ToString(id)` — see Section 13, Error D.
inline std::string ToStdString(SubsystemId id) {
    return std::string(ToString(id));
}
```

### Why each decision was made

**`enum class`, not plain `enum`.** A plain `enum` implicitly converts to `int`. That means
`Register(0, new Store())` would compile, and so would `if (id == 3)`. A scoped enum
(`enum class`) has no implicit conversion — the only thing that fits a `SubsystemId`
parameter is a `SubsystemId`. That refusal to convert *is the feature you are buying*.

**`: std::uint8_t`.** This fixes the underlying storage to one byte. Not a performance
decision at five subsystems — it's a documentation decision. It says "this is a small tag, not
a number you do arithmetic on."

**Returns `std::string_view`, not `std::string`.** `ToString` returns a view into a string
literal that lives in the binary's read-only data for the entire program. Returning
`std::string` would heap-allocate and copy on *every log line*. `std::cout << ToString(id)`
works directly — C++17 added `operator<<` for `string_view`.

**No `default:` case in the switch.** This is deliberate and it is the second-best thing in
this file. With every enumerator listed and no `default`, GCC and Clang emit
`-Wswitch` ("enumeration value 'Foo' not handled in switch") the moment you add a sixth
subsystem to the enum and forget to give it a label. A `default:` case would silence that
warning and let a new subsystem ship with the label `"UnknownSubsystem"`. The bare `return`
*after* the switch satisfies the "all control paths return a value" rule without suppressing
the warning.

**Values match the current strings exactly.** `"Dir Reader"` (with the space), `"Search
Engine"` (not `"Engine"`). Copy them character for character from `directoryreader.cpp:6` and
`engine.cpp:6`. This is what makes the refactor output-preserving and therefore verifiable.

---

## 4. Step 2 — Rewire the Kernal header

**File:** `internal/kernal/kernal.hpp`

Add the include next to the existing core includes (line 8–9 area):

```cpp
#include "internal/kernal/core/headerfiles/subsystem.hpp"
#include "internal/kernal/core/headerfiles/subsystemid.hpp"   // <-- add
#include "internal/kernal/core/headerfiles/error.hpp"
```

Then change the five declarations:

| Line | From | To |
|---|---|---|
| 28 | `Error Register(const std::string& name, Subsystem* subsystem);` | `Error Register(SubsystemId id, Subsystem* subsystem);` |
| 36 | `Subsystem* GetSubsystem(const std::string& name) const;` | `Subsystem* GetSubsystem(SubsystemId id) const;` |
| 40 | `std::map<std::string, std::unique_ptr<Subsystem>> subsystems_;` | `std::map<SubsystemId, std::unique_ptr<Subsystem>> subsystems_;` |
| 41 | `std::vector<std::string> order_;` | `std::vector<SubsystemId> order_;` |
| 45 | `Error StartSubSystem_(std::string& name);` | `Error StartSubSystem_(SubsystemId id);` |

### Three things to notice

**Pass by value, not by reference.** `SubsystemId` is one byte. `const SubsystemId&` would be
a pointer-sized indirection to save one byte — strictly worse. The rule: pass enums, ints, and
pointers by value; pass strings and containers by const reference.

**Line 45 had a latent bug.** `StartSubSystem_(std::string& name)` takes a *non-const*
reference, which means you could never call it with a literal — `StartSubSystem_("Store")`
would not compile. Passing by value makes that problem vanish rather than fixing it.

**`std::map<SubsystemId, ...>` just works.** `std::map` needs `operator<` on its key.
Scoped enums support relational operators between values of the same type, so the default
`std::less<SubsystemId>` is fine. You do **not** need a custom comparator or a hash.

---

## 5. Step 3 — `Register`

**File:** `internal/kernal/kernal.cpp`, lines 25–36.

```cpp
Error Kernal::Register(SubsystemId id, Subsystem* subsystem) {
    if (subsystems_.find(id) != subsystems_.end()) {
        delete subsystem; // avoid leak
        return Error("[" + ToStdString(id) + "] Subsystem already registered");
    }

    subsystems_[id] = std::unique_ptr<Subsystem>(subsystem);
    order_.push_back(id);

    std::cout << "\n [" << GetName() << "] [" << ToString(id)
              << "] Subsystem registered" << std::endl;
    return Error("");
}
```

**Two different conversions, on purpose — learn the difference now:**

- Line 4 uses **`ToStdString`** because it is building a `std::string` with `operator+`.
  `"[" + ToString(id)` does *not* compile (Section 13, Error D).
- The `std::cout` line uses **`ToString`** because streaming a `string_view` is free — no
  allocation. Never use `ToStdString` in a stream chain.

Everything else in this function is unchanged, including the `delete subsystem` on the
duplicate path. That `delete` is still correct and still necessary: `Register` takes ownership
of a raw pointer, so on the early-return path it must free it or leak.

---

## 6. Step 4 — `InitAll`

**File:** `internal/kernal/kernal.cpp`, lines 38–56. Only the loop body changes.

```cpp
    for (const auto& id : order_) {
        Subsystem* subsystem = subsystems_[id].get();
        Error error = subsystem->Init();

        if (HasError(error)) {
            std::cout << "\n [" << GetName() << "] [" << ToString(id)
                      << "] Initialization failed: " << error.GetMessage() << std::endl;
            return Error("[" + ToStdString(id) + "] Initialization failed: "
                         + error.GetMessage());
        }

        std::cout << "\n [" << GetName() << "] [" << ToString(id)
                  << "] Initialization successful" << std::endl;
    }
```

> **Style note:** `const auto& id` is now copying-by-reference a one-byte value. `auto id`
> would be marginally better. Either compiles and neither is wrong; if you change it, change
> it in all three loops for consistency. Consistency beats micro-optimisation.

---

## 7. Step 5 — `StopAll`

**File:** `internal/kernal/kernal.cpp`, lines 94–119.

```cpp
    for (auto it = order_.rbegin(); it != order_.rend(); ++it) {
        const SubsystemId id = *it;
        Subsystem* subsystem = subsystems_[id].get();
        Error error = subsystem->Stop();

        if (HasError(error)) {
            std::cout << "\n [" << GetName() << "] [" << ToString(id)
                      << "] Stop failed: " << error.GetMessage() << std::endl;
            errors += "[" + ToStdString(id) + "] Stop failed: " + error.GetMessage() + "\n";
        } else {
            std::cout << "\n [" << GetName() << "] [" << ToString(id)
                      << "] Stop successful" << std::endl;
        }
    }
```

The line `const std::string& name = *it;` becomes `const SubsystemId id = *it;` — a **value**,
not a reference. Binding a reference to a one-byte enum is pointless indirection.

The reverse iteration (`rbegin`/`rend`) is untouched. Stop order is still the reverse of
registration order, which is still what `order_` holds.

---

## 8. Step 6 — `GetSubsystem` and the destructor

**`GetSubsystem`**, lines 121–127 — a pure type swap:

```cpp
Subsystem* Kernal::GetSubsystem(SubsystemId id) const {
    auto it = subsystems_.find(id);
    if (it != subsystems_.end()) {
        return it->second.get();
    }
    return nullptr;
}
```

**Destructor**, lines 5–15:

```cpp
Kernal::~Kernal(){
    for (auto it = order_.rbegin(); it != order_.rend(); ++it) {
        Subsystem* sub = subsystems_[*it].get();
        if (sub->GetState() == Subsystem::State::STARTED) {
            std::cerr << "\n [" << GetName() << "] WARNING: " << ToString(*it)
                      << " was not stopped before Kernal destruction. "
                      << "Stopping now." << std::endl;
            sub->Stop();
        }
    }
}
```

Only line 9's `<< *it` becomes `<< ToString(*it)`. Miss this one and you get Error C in
Section 13 — the compiler catches it, so it isn't dangerous, just annoying.

---

## 9. Step 7 — `StartAll` and the hidden null-deref

**File:** `internal/kernal/kernal.cpp`, lines 58–92.

Line 63 is the interesting one:

```cpp
Store* store = static_cast<Store*>(GetSubsystem("Store"));   // BEFORE
if (store->HasSearchIndex()){                                // line 65
```

Becomes:

```cpp
Store* store = static_cast<Store*>(GetSubsystem(SubsystemId::Store));

if (store == nullptr) {
    return Error("[" + ToStdString(SubsystemId::Store) + "] not registered");
}

if (store->HasSearchIndex()){
```

**Add the null check.** This is not optional and it is not scope creep — it is a live crash in
your current code. `GetSubsystem` can return `nullptr` (line 126) and line 65 dereferences the
result immediately with no check. Today it survives only because `main.cpp:26` happens to
register a Store before `StartAll` runs. Delete that one line in `main` and `StartAll`
segfaults.

The enum makes the *typo* impossible. It does **not** make the *missing registration*
impossible — that is a runtime fact, and runtime facts need runtime checks. Understanding
where that boundary sits is the most important idea in this document.

> **Also note:** `static_cast` here is only safe because you know `SubsystemId::Store` maps to a
> `Store*`. Nothing enforces that. That unenforced link is exactly what "Level 2 — the type is
> the key" removes, and it's the subject of Part 2.

### The commented-out rollback block (lines 71–88)

It references `name` and `subsystems_[name]`. **Update it now while you're in the file**, even
though it's commented out — a comment block that no longer compiles is a trap for future you:

```cpp
    // for (const auto& id : order_) {
    //     Error error = subsystems_[id]->Start();
    //     if (HasError(error)) {
    //         std::cerr << "\n [" << GetName() << "] [" << ToString(id)
    //                   << "] Start failed, rolling back..." << std::endl;
    //         for (auto it = started.rbegin(); it != started.rend(); ++it) {
    //             std::cerr << "\n [" << GetName() << "] Rolling back ["
    //                       << ToString(*it) << "]" << std::endl;
    //             subsystems_[*it]->Stop();
    //         }
    //         return error;
    //     }
    //     started.push_back(id);
    // }
```

And line 61: `std::vector<std::string> started;` → `std::vector<SubsystemId> started;`.

### Step 8 — `StartSubSystem_` (lines 129–132)

```cpp
Error Kernal::StartSubSystem_(SubsystemId id){
    Error error = subsystems_[id]->Start();
    return error;
}
```

---

## 10. Step 9 — `main.cpp`

**File:** `cmd/main.cpp`. Add the include after line 9:

```cpp
#include "internal/kernal/core/headerfiles/subsystemid.hpp"
```

> You would get it transitively through `kernal.hpp`, but include what you use. Transitive
> includes break silently when someone reorganises a header six months from now.

Then lines 22–39:

```cpp
    // ===== 3. Register subsystems (Kernal takes ownership) =====
    // Order matters! Dependencies must be registered first.

    kernal.Register(SubsystemId::Store,     new Store());
    kernal.Register(SubsystemId::DirReader, new DirectoryReader(dirQueue));
    kernal.Register(SubsystemId::Lexer,     new Lexer(dirQueue, lineQueue, parserQueue));
    kernal.Register(SubsystemId::Parser,    new Parser(parserQueue));

    Store* store = static_cast<Store*>(kernal.GetSubsystem(SubsystemId::Store));
    kernal.Register(SubsystemId::Engine,    new Engine(store));
```

### Two notes on line 38

**`dynamic_cast` → `static_cast`.** Your original used `dynamic_cast`, which performs a runtime
RTTI check and returns `nullptr` on failure — but line 39 never checked the result, so the
safety was paid for and discarded. `static_cast` is honest about what's happening. (The
genuinely correct fix is Section 14.)

**This whole lookup is still a smell.** `main` created the `Store` two lines earlier, handed it
away, and asked for it back. Keeping the pointer directly is better:

```cpp
    Store* store = new Store();
    kernal.Register(SubsystemId::Store, store);   // Kernal takes ownership
    // ... register the middle three ...
    kernal.Register(SubsystemId::Engine, new Engine(store));  // main wires the dependency
```

`store` is a *borrowed* pointer — the `unique_ptr` inside `subsystems_` still owns it, and it
stays valid as long as the Kernal does. Same pattern you already use for the `RingBuffer`s at
`main.cpp:18–20`.

**Do this as a separate commit, after the enum refactor compiles and runs.** One idea per
commit. If you change identity *and* wiring at once and the output shifts, you won't know which
change caused it.

---

## 11. Build system

**Nothing to change in `CMakeLists.txt`.**

`subsystemid.hpp` is a header with no corresponding `.cpp`. There is no new translation unit,
so there is nothing to add to any source list. This is only true because `ToString` and
`ToStdString` are marked `inline` — that keyword is what lets a *function definition* live in a
header. Forget it and you get Error A below.

---

## 12. Verification — the part that makes this provable

Because every enum label was copied character-for-character from the existing
`Name()` overrides and registration literals, **the program's output must be byte-identical
before and after.**

Capture a baseline before you start:

```powershell
# BEFORE any edits — build, run, press Enter at the prompt
.\build_debug\searchengine.exe > "$env:TEMP\before.txt" 2>&1
```

After the refactor:

```powershell
.\build_debug\searchengine.exe > "$env:TEMP\after.txt" 2>&1
Compare-Object (Get-Content "$env:TEMP\before.txt") (Get-Content "$env:TEMP\after.txt")
```

**Empty output from `Compare-Object` means the refactor is correct.** Any difference is a
mistake — almost certainly a mistyped label in `ToString`.

### Why the map's key ordering doesn't affect this

`std::map<std::string, ...>` sorts keys lexicographically: `Dir Reader`, `Lexer`, `Parser`,
`Search Engine`, `Store`. `std::map<SubsystemId, ...>` sorts by enum value: `Store`,
`DirReader`, `Lexer`, `Parser`, `Engine`. **The iteration order of `subsystems_` changes.**

That is safe here, and you can prove it: every loop in `kernal.cpp` — the destructor (line 6),
`InitAll` (line 41), `StopAll` (line 100) — iterates **`order_`**, never `subsystems_`. The map
is used only for point lookups via `find` and `operator[]`, and lookup doesn't care about
ordering. `order_` is a `std::vector` that preserves registration order, and that is unchanged.

> **Standing rule from here on:** never iterate `subsystems_` directly. Iterate `order_` and
> look up. The map holds *ownership*; the vector holds *sequence*. Mixing those up is how you
> get a subsystem started before its dependency.

### Manual checklist

- [ ] `grep -rn '"Store"' internal/ cmd/` returns **exactly one** hit: `subsystemid.hpp`
- [ ] Same for `"Lexer"`, `"Parser"`, `"Dir Reader"`, `"Search Engine"` — one hit each
- [ ] `grep -rn 'GetSubsystem("' internal/ cmd/` returns **zero** hits
- [ ] Build is clean with **no new warnings** (build with `-Wall`)
- [ ] `Compare-Object` is empty

> **Wait — `store.cpp:4` returns `"Store"` too.** Correct, and that's a real remaining
> duplicate. The five `Name()` overrides are the last place labels are hardcoded. Section 14
> covers eliminating them. For now, expect **two** hits per label (`subsystemid.hpp` + the
> subsystem's own `.cpp`), and treat any *third* hit as a bug.

---

## 13. Compile-error decoder

You will hit at least one of these. That's the system working.

### Error A — `multiple definition of 'ToString(SubsystemId)'`
```
ld: internal/store/store.cpp.obj: multiple definition of `ToString(SubsystemId)';
    cmd/main.cpp.obj: first defined here
```
**Cause:** You forgot `inline` on `ToString` or `ToStdString`.
**Why:** A non-inline function *definition* in a header is compiled into every `.cpp` that
includes it. The linker then finds five copies and can't choose. `inline` tells the linker
"these are all the same, keep one."
**Fix:** Add `inline`.

### Error B — `cannot convert 'const char [6]' to 'SubsystemId'`
```
error: could not convert '"Store"' from 'const char [6]' to 'SubsystemId'
```
**Cause:** A `Register(...)` or `GetSubsystem(...)` call still passes a string literal.
**This is the error you built the whole system to get.** The compiler is pointing at exactly
the line and exactly the mistake.
**Fix:** Replace with `SubsystemId::Store`.

### Error C — `no match for 'operator<<'`
```
error: no match for 'operator<<' (operand types are 'std::ostream' and 'SubsystemId')
```
**Cause:** You're streaming a raw id into `cout` — a log line you missed.
**Why:** Scoped enums deliberately have no implicit conversion to int, so `ostream` has no
overload. This is the "no implicit conversion" property protecting you.
**Fix:** Wrap it: `<< ToString(id)`.

### Error D — `invalid operands to binary expression ('const char[2]' and 'std::string_view')`
```
error: no match for 'operator+' (operand types are 'const char [2]' and 'std::string_view')
```
**Cause:** You wrote `"[" + ToString(id) + "]"`.
**Why:** C++17 defines no `operator+` between `std::string` (or `const char*`) and
`std::string_view`. This surprises everyone once. It was omitted deliberately — `string_view`
is a non-owning view, and `+` must produce an owning result, so the committee made you say so
explicitly.
**Fix:** Use `ToStdString(id)` in `+` chains. Use `ToString(id)` in `<<` chains.

### Error E — `enumeration value 'X' not handled in switch` (warning)
```
warning: enumeration value 'Ranker' not handled in switch [-Wswitch]
```
**Cause:** You added an enumerator and forgot its label.
**This warning is the second-biggest prize in this refactor.** It is why `ToString` has no
`default:` case. Treat it as an error.
**Fix:** Add the `case`.

### Error F — `passing 'const Kernal' as 'this' argument discards qualifiers`
**Cause:** You used `subsystems_[id]` inside `GetSubsystem`, which is `const`.
**Why:** `std::map::operator[]` *inserts* a default-constructed value when the key is missing,
so it can't be const. That's also a lurking bug in non-const code — see Section 15.
**Fix:** Use `.find()`, as the original code correctly did.

---

## 14. What this fixed, and what it did not

### Fixed
- ✅ A misspelled subsystem is a **compile error** naming the exact line.
- ✅ Identity and label are different types and cannot be confused.
- ✅ Adding a subsystem without a label is a **compiler warning**.
- ✅ The set of subsystems is documented in one enum instead of scattered literals.
- ✅ You added a real null check at `kernal.cpp:65`.

### Not fixed
- ❌ **Missing registration is still a runtime `nullptr`.** `GetSubsystem(SubsystemId::Engine)`
  before Engine is registered still returns null. The enum proves the *name* is valid, not that
  the *object* exists. Compile-time checking bounds what you can *ask*; it cannot know what you
  *did*.
- ❌ **The `static_cast` is still unchecked.** Nothing enforces that `SubsystemId::Store` maps
  to a `Store*`. Register a `Parser` under `SubsystemId::Store` and you get undefined
  behaviour. → Part 2.
- ❌ **`Name()` still duplicates the label.** `store.cpp:4` still hardcodes `"Store"`.
- ❌ **`kernal.cpp:3` still includes `store.hpp`.** Your control plane depends on a concrete
  subsystem — a layering inversion. It exists only to serve line 63.

---

## 15. Optional hardening (do these *after* the migration is verified)

**1. Replace `subsystems_[id]` with `subsystems_.at(id)`** in `InitAll`, `StopAll`, the
destructor, and `StartSubSystem_`. `operator[]` on a missing key silently inserts a *null*
`unique_ptr`, and the very next `->` is undefined behaviour. `.at()` throws
`std::out_of_range` instead. Loud beats silent.

**2. Assert that key and label agree.** In `Register`, before insertion:

```cpp
    if (subsystem->Name() != ToString(id)) {
        delete subsystem;
        return Error("[" + ToStdString(id) + "] label mismatch: subsystem reports '"
                     + subsystem->Name() + "'");
    }
```

This closes the drift loop *today*, while `Name()` still exists.

---

## 16. Exercises

**Exercise 1 (10 min) — Feel the compile error.**
Change one `SubsystemId::Store` to `SubsystemId::Storee` and build. Read the error. Note that
it names the file, the line, and the misspelling. Compare that to what your *old* code did with
`"Storee"`: compiled, linked, ran, crashed somewhere else. Revert.

**Exercise 2 (10 min) — Feel the warning.**
Add `Ranker` to the enum, don't add a `case` to `ToString`, build with `-Wall`. Read the
`-Wswitch` warning. Now add a `default: return "Unknown";` and rebuild — the warning vanishes
and you've silently broken future-you. Revert both changes and write down why `ToString` has
no `default`.

**Exercise 3 (20 min) — Eliminate `Name()`.**
Give `Subsystem` a private `SubsystemId id_`, have `Kernal::Register` set it, and reimplement
`Name()` in the base class as `return std::string(ToString(id_));` — then delete all five
overrides. Questions to answer in writing before you code:
- How does `Register` set a private member of `Subsystem`? (`friend`? a protected setter? a
  parameter on a base constructor?) What does each choice cost?
- `Subsystem::Init()` calls `Name()` at `subsystem.hpp:22`. Is `id_` guaranteed set by then?
  Trace the call order: `Register` → `InitAll` → `Init`. Where is the window?
- If a `Subsystem` is constructed but never registered and something calls `Name()`, what does
  it return? Is that acceptable?

**Exercise 4 (written, no code) — Find the boundary.**
List every remaining way to break subsystem lookup at runtime after this refactor. For each,
state whether a *compile-time* mechanism could catch it. This is the design brief for Part 2.

---

## 17. Commit sequence

Do not do this in one commit. Four commits, each independently buildable and runnable:

1. `Add: SubsystemId enum and ToString label mapping` — new header only. Nothing uses it yet.
   Builds clean.
2. `Refactor: Kernal keyed by SubsystemId instead of string` — Sections 4–10. **Verify
   `Compare-Object` is empty here.**
3. `Fix: null check on Store lookup in StartAll` — the crash from Section 9.
4. `Refactor: wire Engine's Store dependency directly in main` — Section 10's second note.

If step 2 changes the output, `git diff` against step 1 is a five-file diff you can read in a
minute. Bundle all four and you're bisecting a 200-line change.

---

## 18. Where this sits

| Level | Key type | Typo caught? | Wrong type caught? | Doc |
|---|---|---|---|---|
| 0 | `std::string` literal | ❌ runtime null | ❌ | your code today |
| 0.5 | named `string_view` constant | ✅ compile | ❌ | Section 0 discussion |
| **1** | **`enum class SubsystemId`** | **✅ compile** | **❌** | **this document** |
| 2 | the C++ type itself | ✅ compile | ✅ compile | Part 2 |

Level 2 makes `kernal.Get<Store>()` return a `Store*` with no cast and no id — the type *is*
the key. It needs `typeid` and type erasure, which is why it's a separate lesson. Do Level 1
first; the discipline of separating identity from label is what makes Level 2 make sense.

**Related:** `ownership-and-lifecycle-part1.md` (who owns subsystems),
`startup-01-boot-architecture.md` (registration order and boot sequence).
