# Lesson 12 — Glossary

| Term | One line | § |
|---|---|---|
| smart pointer | an RAII type that owns a pointer and frees it automatically | 12.1 |
| `unique_ptr` | exclusive owner; move-only (copy deleted); zero overhead over a raw ptr | 12.1 |
| exclusive ownership | exactly one owner; ownership transfers via `std::move` | 12.1 |
| `release()` | give up ownership, return the raw pointer (caller must free) | 12.1 |
| `reset(p)` | adopt `p`, delete the previously-held object | 12.1 |
| custom deleter | how a `unique_ptr`/`shared_ptr` frees (e.g. `fclose`, pool dealloc) | 12.1 |
| `make_unique` | create a `unique_ptr` without a visible `new` (exception-safe) | 12.1 |
| `shared_ptr` | shared owner; a reference count frees the object at the last owner | 12.2 |
| control block | heap object holding strong count, weak count, object/deleter | 12.2 |
| strong count | number of `shared_ptr`s owning the object; object dies at 0 | 12.2 |
| weak count | number of `weak_ptr`s; control block dies at strong==0 AND weak==0 | 12.2/12.3 |
| `use_count()` | the current strong count | 12.2 |
| `make_shared` | one allocation for object + control block (vs two) | 12.2 |
| aliasing shared_ptr | owns one object while storing a pointer to another (member) | 12.2 |
| reference cycle | A→B→A via shared_ptr; counts never reach 0 → leak | 12.3 |
| `weak_ptr` | non-owning observer; doesn't change the strong count | 12.3 |
| `lock()` | atomically get a `shared_ptr` if alive, else an empty one | 12.3 |
| `expired()` | true once the object is destroyed (use_count()==0) | 12.3 |
| temporary ownership | access the object only if it still exists, via `lock()` | 12.3 |
| atomic refcount | counts are `std::atomic`; concurrent copies of distinct shared_ptrs are safe | 12.4 |
| count-safe ≠ object-safe | shared_ptr protects the count, NOT access to the object | 12.4 |
| `enable_shared_from_this` | lets an object hand out a shared_ptr to its existing control block | 12.4 |
| own vs borrow | own with smart pointers; borrow (params) with raw `T*`/`T&` | 12.4/12.5 |
| escaped `.get()` | a raw pointer kept past the owner's lifetime → dangling/UAF | 12.5 |
