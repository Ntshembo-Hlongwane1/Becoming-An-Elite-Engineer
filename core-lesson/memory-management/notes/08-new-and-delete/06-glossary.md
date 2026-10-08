# Lesson 8 — Glossary

| Term | One line | § |
|---|---|---|
| new-expression | `new T` — allocate storage via `operator new`, then construct | 8.1 |
| two steps of `new` | step 1 allocate (`operator new`), step 2 construct (placement new) | 8.1 |
| two steps of `delete` | step 1 destruct (`~T()`), step 2 deallocate (`operator delete`) | 8.1 |
| allocation function | `operator new` / `operator new[]`; raw, aligned storage (≈ malloc) | 8.2 |
| deallocation function | `operator delete` / `operator delete[]`; all `noexcept` | 8.2 |
| `std::bad_alloc` | exception thrown by throwing `operator new` on failure | 8.2 |
| nothrow new | `new (std::nothrow) T` — returns `nullptr` instead of throwing | 8.2 |
| new-handler | callback `operator new` invokes before throwing `bad_alloc` | 8.2 |
| `__STDCPP_DEFAULT_NEW_ALIGNMENT__` | default alignment `operator new` guarantees (16 here) | 8.2 |
| over-aligned type | `alignof` > default new alignment → uses `operator new(size, align_val_t)` | 8.2 |
| aligned new (C++17) | `operator new(size_t, std::align_val_t)` for over-aligned types | 8.2 |
| class-specific `operator new` | a type's own static allocator hook; used by `new ThatType` | 8.2 |
| placement new | `new (ptr) T` — construct at `ptr`, no allocation (`<new>`) | 8.3 |
| non-allocating form | `operator new(size_t, void*)` returns its pointer arg unchanged | 8.3 |
| manual destructor call | `p->~T()` — required after placement new; frees nothing | 8.3 |
| `std::construct_at` / `destroy_at` | C++20 named forms of placement-new / explicit destroy | 8.3 |
| `std::launder` | optimiser barrier after replacing an object in reused storage | 8.3 |
| array cookie | extra bytes `new[]` stores to record the element count (8 B here) | 8.1/8.5 |
| replaceable function | global `operator new`/`delete` a program may redefine whole-program | 8.4 |
| sized delete | `operator delete(void*, size_t)`; compiler passes the size back | 8.1/8.4 |
| size header | per-allocation size stored before the pointer (like a chunk header) | 8.4 |
| `constinit` | constant-initialised global; ready before any dynamic init allocates | 8.4 |
| matched set | replace new/delete, new[]/delete[], sized/nothrow together | 8.4 |
| alloc-dealloc mismatch | `new`/`delete[]` (or family) crossed — UB; ASan names it | 8.5 |
| double-delete / use-after-delete | the Lesson-7.5 heap primitives, spelled in `delete` | 8.5 |
