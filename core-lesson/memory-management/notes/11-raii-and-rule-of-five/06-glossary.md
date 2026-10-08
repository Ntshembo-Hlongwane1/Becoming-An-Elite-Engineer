# Lesson 11 — Glossary

| Term | One line | § |
|---|---|---|
| RAII | resource acquired in ctor, released in dtor; freed on every exit incl. unwinding | 11.1 |
| ownership | one object responsible for releasing a resource exactly once | 11.1 |
| rule of zero | own nothing raw; build from RAII members → compiler-generated specials are correct | 11.1 |
| special member functions | dtor, copy ctor, copy assign, move ctor, move assign | 11.1 |
| stack unwinding | exception propagation that runs locals' destructors on the way out | 11.1 |
| shallow copy | copies the handle/pointer → two owners of one resource (bug for owners) | 11.2 |
| deep copy | allocates own resource and copies the contents (value semantics) | 11.2 |
| value semantics | copies are independent; objects behave like `int`s | 11.2 |
| self-assignment guard | `if (this != &o)` so `a = a` / `v = std::move(v)` is safe | 11.2/11.3 |
| rule of three | need a custom dtor ⇒ need custom copy ctor + copy assign too | 11.2 |
| `= delete` (specials) | forbid copy/move at compile time instead of doing it wrong | 11.2 |
| rvalue / lvalue | temporary/expiring value vs named reusable object | 11.3 |
| rvalue reference (`T&&`) | binds to rvalues; selects the move overload | 11.3 |
| `std::move` | a cast to rvalue — permits stealing; moves nothing itself | 11.3 |
| move constructor | steal the source's resource; leave it valid-but-indeterminate | 11.3 |
| valid but indeterminate | moved-from state: destructible/assignable, value unspecified | 11.3 |
| rule of five | declaring a dtor suppresses implicit moves ⇒ declare all five | 11.3 |
| implicit move suppression | user dtor/copy/move-assign stops the compiler generating moves | 11.3 |
| `noexcept` move | lets containers move (not copy) on reallocation | 11.3/11.4 |
| NRVO / copy elision | returning a local by name elides the copy/move | 11.3 |
| `return std::move(x)` | anti-pattern: defeats NRVO, forces an extra move | 11.3 |
| exception-safety guarantees | no-throw / strong / basic / none | 11.4 |
| strong guarantee | commit-or-rollback: success, or throw with no observable effect | 11.4 |
| basic guarantee | no leaks, invariants intact, values may change | 11.4 |
| copy-and-swap | assign via copy-ctor then noexcept swap → strong guarantee, self-safe | 11.4 |
| `std::move_if_noexcept` | move if the move is noexcept, else copy (preserve strong guarantee) | 11.4 |
| relocation | growing a container: new buffer, construct across, destroy old | 11.4 |
