# 12.5 — The Security Researcher's View: ownership made safe (and where it still bites)

Lesson 11.5 showed that most real-world double-free / use-after-free bugs are *broken ownership* in a
hand-written rule of five. Smart pointers are the fix: they encapsulate that rule of five **once**,
correctly, so the types that use them get the rule of zero and can't double-free or leak. But they are
not a magic shield — the moment you reach back to a raw pointer, or build a cycle, the old hazards
return. A researcher needs both halves: where smart pointers *remove* bugs, and where they *relocate*
them. Three hats.

## What they fix (by construction)
- **Double-free / use-after-free from ownership confusion.** `unique_ptr`'s deleted copy makes "two
  owners of one allocation" a *compile error* (Lesson 12.1 §3); `shared_ptr`'s refcount makes "freed
  while still referenced" impossible — the object lives exactly until the last owner leaves (Lesson
  12.2). The Lesson-7.5 heap primitives, when they come from application ownership bugs (the common
  case), simply stop existing.
- **Leaks on exception paths.** RAII destructors run during unwinding (Lesson 11.1), so an exception
  between acquire and release can't leak — the `unique_ptr`/`shared_ptr` frees on the way out.

## Where they still bite (what a researcher looks for)
- **Raw `.get()` / `&*sp` escaping the owner's lifetime.** Smart pointers own; the moment you extract
  a raw `T* r = sp.get()` (or a reference) and keep it past the point where the owner is
  reset/reassigned/destroyed, you have a **dangling pointer** — a use-after-free the smart pointer
  can't prevent because you left its protection. Grep target: a `.get()` or `&*` whose result outlives
  a `reset()`/reassignment/scope of the owner.
- **`weak_ptr` misused as a raw back-pointer, or dereferenced via a racy check.** `lock()` exists
  precisely so you don't test-then-use; code that does `if(!w.expired()) use(*w.lock())` across threads
  (or caches the `lock()` result too long) reintroduces a UAF window (Lesson 12.3 §4). A *raw*
  back-pointer where a `weak_ptr` belongs is a classic dangling parent pointer.
- **The cycle leak as a DoS.** An orphaned `shared_ptr` cycle (Lesson 12.3 §1) never frees — unbounded
  memory growth an attacker can drive (e.g. feeding inputs that build self-referential structures) is
  a denial-of-service. "No ASan error but RSS climbs" is its signature; leak sanitizers report the
  cycle's blocks as "still reachable/leaked" at exit.
- **Second control block from a raw `this`.** `shared_ptr<T>(this)` (or from any already-owned raw
  pointer) builds a *second* refcount → double-free when both reach zero (Lesson 12.4 §4). A type that
  hands out `shared_ptr`s to itself without `enable_shared_from_this` is a reliable double-free.
- **Thread confusion.** Believing `shared_ptr<T>` makes `T` thread-safe (it only makes the *count*
  safe, Lesson 12.4 §1) → unsynchronised object access → data race → memory corruption under load.
- **Aliasing constructor / custom deleter mismatches.** The stored pointer differing from the managed
  pointer (Lesson 12.2) and a custom deleter that doesn't match how the object was allocated (Lesson
  8.5's family mismatch, now hidden in a deleter) are subtle sources of corruption.

## Offense — discovery & honest ceiling
- **Discovery:** read for the boundary between "owned" and "raw" — `.get()`, `release()`, `&*`,
  raw `this` passed where a `shared_ptr` is expected, `weak_ptr` vs raw back-pointers, and any
  `shared_ptr` cycle in the type graph. Dynamically: ASan catches the resulting UAF/double-free with
  allocation+free stacks; LSan/valgrind catch the cycle leak at exit; TSan catches the "thought it was
  thread-safe" races.
- **Ceiling:** a UAF reached through an escaped `.get()` is the *same* Lesson-7.5 primitive (arbitrary
  reuse → potential arbitrary write / RCE) — smart pointers didn't weaken it, you just stepped outside
  them. The cycle leak tops out at DoS. As always, name the exact escape (which `.get()`, which cycle)
  and what still gates escalation (hardening + an info leak).

## Defense — the discipline
- **Own with smart pointers, borrow with raw.** Ownership = `unique_ptr`/`shared_ptr` members and
  returns; *use-only* parameters = `T*`/`T&` that never outlive the call (Lesson 12.4 §3). Never store
  a borrowed raw pointer past the owner's lifetime.
- **Default `unique_ptr`; `shared_ptr` only for real shared ownership; `weak_ptr` for every
  back-reference/observer** so cycles can't form (Lesson 12.3). Audit the type graph for `shared_ptr`
  cycles.
- **Never make a second owner from a raw pointer** — `make_shared`/`shared_from_this`, not
  `shared_ptr(this)` (Lesson 12.4 §4).
- **Match custom deleters to the allocation** (Lesson 8.5); treat `.get()`/`release()` as "I am now
  outside RAII — audit this" markers.
- **Tooling in CI:** ASan+UBSan (every exercise here), LSan for the cycle leaks, TSan for the
  count-vs-object thread confusion, and clang-tidy's lifetime/ownership checks. "By construction"
  means ownership is expressed in the types, and the few raw-pointer escapes are deliberate and
  audited.

## Through-line to the capstone
Your detector finds UAF/double-free at runtime; this lesson shows the dominant *source* of those in
C++ (ownership expressed wrong, or escaped via raw pointers), so the fix is usually "use the right
smart pointer / stop leaking a `.get()`," not a band-aid. And the `SharedPtr`/`WeakPtr` you build here
are the reference-counting and weak-observer mechanisms you'll recognise inside allocators, caches,
and the browser/engine codebases you'll reverse — where a mis-modeled cycle or an escaped raw pointer
is exactly the bug class worth hunting.

## My summary
