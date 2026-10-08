# 11.5 — The Security Researcher's View: broken ownership is the bug

Lessons 7–8 showed the heap primitives (double-free, UAF, overflow). This lesson reveals *where they
come from in real C++ code*: not from exotic attacks but from **broken ownership** — a type that owns
a resource but gets its rule of three/five wrong. Every such slip is one of the Lesson-7.5 primitives,
handed to an attacker through ordinary-looking application code. And RAII, done right, is the fix that
removes the whole class. Three hats.

## The bug classes (each = a specific missing/wrong special member)
- **Missing/shallow copy → double-free + aliasing (§11.2).** An owner with a defaulted copy (someone
  wrote the destructor, forgot the copy ctor — rule of three violated) shallow-copies its pointer:
  two objects, one allocation, two destructors → **double-free** (Lesson 7.5's exact primitive). The
  aliasing also means writing object A corrupts object B's data — a confused-deputy within the heap.
- **Use-after-move (§11.3).** Using a moved-from object as if it still held its resource — e.g.
  reading `s1` after `auto s2 = std::move(s1);` on a type whose moved-from state is empty — is a
  logic-level UAF/null-deref. A *self-move* (`v = std::move(v)`) without the `this != &o` guard frees
  the buffer then reads it: UAF.
- **Exception-unsafe operation → leak or broken invariant (§11.4).** A `reserve`/assignment that frees
  the old buffer before the new one is ready, then throws, leaves a **dangling** member pointer (next
  use = UAF) or leaks the resource — the "none" guarantee. Leaks are a DoS vector; the dangling
  pointer is worse.
- **Throwing destructor.** A destructor that throws during stack unwinding calls `std::terminate`
  (crash/DoS), and a half-torn-down object can leave the program in an exploitable inconsistent state.

## Offense — discovery
- **Source/RE:** the tell is a class with a **user-declared destructor** (owns something) but **not**
  the matching copy/move — grep for `~Name()` with `delete`/`free`/`close` in the body, then check
  whether copy ctor, copy assign, move ctor, move assign are all declared or `=delete`d. Any gap is a
  candidate double-free/UAF. Also: `operator=` that `delete`s before allocating; moves lacking the
  self-assignment guard; any `std::move(x)` followed by a later use of `x`.
- **Dynamic:** ASan catches the fallout precisely — `attempting double-free`, `heap-use-after-free`
  (with the move/free stack). Fuzzing a type through copy/move/assign/clear sequences under ASan
  surfaces missing-rule-of-five bugs fast; this is a standard way such bugs are found in the wild.
- **The clang-tidy / compiler angle:** `-Wdeprecated-copy`, `clang-tidy`'s
  `cppcoreguidelines-special-member-functions` flag exactly the rule-of-five gaps — a researcher
  reads those warnings on a target's source as a bug map.

## Offense — value, honest ceiling
- **Ceiling:** a double-free or UAF from broken ownership is the **same** primitive as a hand-crafted
  heap bug (Lesson 7.5): with heap grooming it can become arbitrary write / code execution. The
  difference is it's reached through normal API use (copy this object, move that one), so it's often
  easier to trigger than a memory-corruption bug you have to massage.
- **Floor / obstacles:** modern allocator hardening (Lesson 7.5) still applies, so escalation usually
  needs a companion leak; and if the type is behind values that are never copied/moved across a trust
  boundary, the bug may be unreachable. The honest statement names the member that's wrong and the
  API path that reaches it.

## Defense — RAII makes the whole class unrepresentable
- **Rule of zero (§11.1 §4): own nothing raw.** Build every type out of `std::vector`/`std::string`/
  `std::unique_ptr`/`std::shared_ptr` (Lesson 12). The compiler then generates correct copy/move/destroy,
  each resource is freed exactly once, and there is simply **no** hand-written `delete` to double or
  skip. This single habit eliminates the majority of real-world double-free/UAF.
- **Rule of five, written once, where ownership lives.** In the rare resource-owning type (allocator,
  smart pointer, container — this lesson's `Vector`), implement all five correctly: deep copy, stealing
  `noexcept` move, self-assignment guards, copy-and-swap for the strong guarantee. Then everything built
  on it is rule-of-zero-safe.
- **`=delete` to forbid, not to guess.** A unique owner should `=delete` its copies so misuse is a
  **compile error**, never a runtime double-free (§11.2 §4).
- **Strong guarantee (§11.4)** so a throw can't leave a dangling member pointer; `noexcept`
  destructors/moves so unwinding can't `terminate`.
- **Tooling in CI:** ASan/UBSan on tests (every exercise here), plus the rule-of-five lint. "By
  construction" here means: the types that own resources are few, audited, and tested; everything else
  can't leak or double-free because it owns nothing raw.

## Through-line to the capstone
Your detector finds double-free/UAF at runtime; this lesson shows the *source-level* cause the
detector's reports point back to, so you can fix the class, not just the symptom. And the `Vector<T>`
you build here is the first type where *you* are the author responsible for the five members being
right — the same responsibility you'll carry in the smart pointers (Lesson 12) and the allocator
hardening (Lesson 17). Ownership correctness is where most C++ security actually lives.

## My summary
