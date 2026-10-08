# Lesson 7 — Glossary

| Term | One line | § |
|---|---|---|
| heap (region) | runtime-growable address-space region `malloc` carves; not the heap data structure | 7.1 |
| `malloc`/`free` | user-space library code that sub-divides kernel slabs; not a syscall | 7.1 |
| program break | current top of the brk-heap; `sbrk`/`brk` move it, `sbrk(0)` reads it | 7.1 |
| `sbrk`/`brk` | raise/lower the break to grow/shrink the heap; malloc's small-request source | 7.1 |
| `mmap` (for malloc) | private anonymous mapping used for any single request ≥ MMAP_THRESHOLD | 7.1 |
| `MMAP_THRESHOLD` | size (128 kB default) above which malloc uses mmap not the break | 7.1 |
| `free(NULL)` | defined no-op | 7.1 |
| `malloc(0)` | returns a unique, freeable pointer you must not dereference | 7.1 |
| chunk | one allocation unit: header + payload, size a multiple of 16 | 7.2 |
| header | per-chunk bookkeeping before the payload: size + flag bits | 7.2 |
| payload | the bytes handed to you; reused to store fd/bk when the chunk is free | 7.2 |
| user ptr vs chunk ptr | returned pointer vs header start (user ptr − overhead) | 7.2 |
| size-field flag bits | `PREV_INUSE`(0x1), `IS_MMAPPED`(0x2), `NON_MAIN_ARENA`(0x4) in size's low bits | 7.2 |
| `PREV_INUSE` (P) | bit saying the physically-previous chunk is in use | 7.2/7.4 |
| boundary tag | size stored at chunk edges so both neighbours are reachable in O(1) | 7.2 |
| alignment (16) | returned pointers aligned to `alignof(max_align_t)` = 16 here | 7.2 |
| minimum chunk size | smallest legal chunk — must hold header + two free-list pointers | 7.2 |
| `malloc_usable_size` | payload bytes a chunk actually provides (≥ requested) | 7.2 |
| free list | linked list of available chunks, threaded through their payloads | 7.3 |
| first-fit / best-fit | take first big-enough chunk / smallest big-enough chunk | 7.3 |
| split | carve a request-sized chunk off an oversized free chunk; remainder stays free | 7.3 |
| fastbins | singly-linked small-chunk lists; no immediate coalescing (speed) | 7.3 |
| unsorted/small/large bins | staging list / exact-size lists / sorted range lists | 7.3 |
| tcache | per-thread cache of small bins in front of the arena (glibc ≥ 2.26) | 7.3 |
| top chunk / wilderness | chunk bordering the raw slab; grown via sbrk, trimmed to the OS | 7.3/7.4 |
| coalesce (consolidate) | merge a freed chunk with free neighbours into one larger chunk | 7.4 |
| internal fragmentation | waste inside a chunk (rounding, minimum size, overhead) | 7.4 |
| external fragmentation | enough total free memory, but no single piece big enough | 7.4 |
| `M_TRIM_THRESHOLD` | top-of-heap free size (128 kB) above which free trims back via sbrk | 7.4 |
| heap overflow | write past a payload into the next chunk's header (metadata corruption) | 7.5 |
| use-after-free (UAF) | use a chunk after free; reads leak fd/bk, writes edit free-list links | 7.5 |
| double-free | free a chunk twice → same address aliased to two live objects | 7.5 |
| tcache poisoning / fastbin dup | corrupt a bin's fd pointer so malloc returns an attacker address | 7.5 |
| safe-linking | glibc ≥2.32 XOR-obfuscation of singly-linked fd pointers | 7.5 |
| hardened_malloc | allocator with out-of-line metadata; overflow can't reach a header | 7.5 |
