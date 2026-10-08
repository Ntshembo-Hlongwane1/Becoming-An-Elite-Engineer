# Lesson 13 — Glossary

| Term | One line | § |
|---|---|---|
| size | number of elements currently in the container | 13.1 |
| capacity | number of element slots currently allocated (≥ size) | 13.1 |
| spare capacity | capacity − size; lets push_back avoid allocating | 13.1 |
| geometric growth | capacity ×constant on realloc (2× libstdc++, 1.5× MSVC) | 13.1 |
| amortised O(1) | average push_back cost, thanks to geometric growth | 13.1 |
| reallocation | allocate bigger buffer, relocate all elements, free old | 13.1 |
| `reserve(n)` | pre-allocate capacity for n; avoids later reallocation | 13.1 |
| `shrink_to_fit` | non-binding request to reduce capacity toward size | 13.1 |
| small-buffer optimization (SBO) | inline storage in the object for the small case | 13.2 |
| small-string optimization (SSO) | SBO for std::string (libstdc++: cap 15, no heap) | 13.2 |
| inline buffer | the in-object fixed array holding small data | 13.2/13.4 |
| SBO trade-off | bigger object + a branch, vs avoided heap allocation | 13.2 |
| iterator/pointer/reference invalidation | a handle naming a moved/removed element | 13.3 |
| invalidation rule (vector) | realloc invalidates all; else push_back only end() | 13.3 |
| dangling handle | iterator/pointer/reference to freed/old storage → UAF | 13.3 |
| stable-reference container | list/map: insert invalidates nothing | 13.3 |
| `SmallVector<T,N>` | inline storage for N + geometric heap growth past N | 13.4 |
| is_inline | whether data_ points at the object's own inline buffer | 13.4 |
| spill | the first growth from inline storage to the heap | 13.4 |
| inline-move relocation | moving an inline source must move elements, not steal a ptr | 13.4 |
| heap-move steal | moving a spilled source steals the buffer pointer (vector-style) | 13.4 |
| held-across-mutation | the common UAF: a handle used after a container mutation | 13.5 |
| bounds-checked access | `.at()` / explicit check vs unchecked `operator[]` | 13.5 |
