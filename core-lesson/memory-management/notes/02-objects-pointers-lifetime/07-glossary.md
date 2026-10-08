# Lesson 2 — Glossary

| Term | One line | § |
|---|---|---|
| object | a region of storage with a type, value, and lifetime | 2.1 |
| storage duration | how long storage exists: static / thread / automatic / dynamic | 2.1 |
| lifetime | begins when storage is obtained+initialized, ends at destruction/reuse | 2.1 |
| trivially-copyable | can be copied with `memcpy` and stay valid (`is_trivially_copyable_v`) | 2.1 |
| pointer | a value holding the address of a byte/object | 2.2 |
| null pointer | `nullptr`; points at no object; deref is UB | 2.2 |
| dangling pointer | holds the address of an object whose lifetime ended | 2.2, 2.5 |
| `void*` | an address with no pointee type; cast before use, no arithmetic | 2.2 |
| pointer-to-pointer | `T**`; out-params, arrays of pointers (`argv`) | 2.2 |
| reference | an alias for an existing object; not null, not rebindable, not an object | 2.2 |
| `const T*` vs `T* const` | pointer-to-const vs const-pointer (read right-to-left) | 2.2 |
| array decay | array name becomes a pointer to its first element; length lost | 2.3 |
| pointer scaling | `p + n` moves `n * sizeof(*p)` bytes | 2.3 |
| one-past-the-end | the index-`n` pointer: legal to form/compare, UB to dereference | 2.3 |
| `ptrdiff_t` | signed type of a pointer difference | 2.3 |
| out-of-bounds (OOB) | access outside `[0,n)`; UB; ASan heap/stack/global-buffer-overflow | 2.3, 2.6 |
| strict aliasing | may read an object only through a *type-accessible* glvalue | 2.4 |
| type-accessible | the object's own type, its signed/unsigned twin, or char/uchar/byte | 2.4 |
| type punning | reading one type's bytes as another; UB unless via memcpy/bit_cast | 2.4 |
| `std::memcpy` pun | the portable, defined way to reinterpret bytes (into a real object) | 2.4 |
| `std::bit_cast` | C++20 constexpr same-size reinterpret, no UB | 2.4 |
| `std::launder` | re-fetch the object actually at an address after storage reuse | 2.4 |
| use-after-scope | dangling via a returned/escaped local; ASan stack-use-after-return | 2.5 |
| use-after-free (UAF) | using heap memory after `delete`/`free`; ASan heap-use-after-free | 2.5, 2.6 |
| double free | freeing the same block twice; corrupts the allocator | 2.5, 2.6 |
| quarantine | holding freed memory poisoned so UAF is caught (ASan / your capstone) | 2.5 |
| type confusion | strict-aliasing violation weaponised: one type's bytes as another's object | 2.6 |
