# Lesson 4 — Glossary

| Term | One line | § |
|---|---|---|
| virtual address | per-process address your program uses | 4.1 |
| physical address | an actual RAM-chip location | 4.1 |
| MMU | hardware that translates virtual→physical every access | 4.1 |
| page | fixed aligned block of virtual memory (4096 B here) | 4.1 |
| page frame | the physical block a page maps to | 4.1 |
| page table | per-process map virtual page → frame + permission bits | 4.1 |
| PTE | page-table entry: frame number + present/writable/NX/dirty bits | 4.1 |
| TLB | cache of recent translations; hit ~1 cycle, miss = page-table walk | 4.1 |
| page fault | trap when a touched page's PTE is not present | 4.2 |
| demand paging | frames attached only on first touch (lazy allocation) | 4.2 |
| minor / major fault | satisfied without / with disk I/O | 4.2 |
| VSZ (VmSize) | total virtual size of all VMAs | 4.2 |
| RSS (VmRSS) | physical RAM currently backing the process | 4.2 |
| mincore | syscall: which pages of a mapping are resident | 4.2 |
| copy-on-write (COW) | shared pages copied only when written | 4.3 |
| MAP_SHARED / MAP_PRIVATE | writes shared / writes private (COW) | 4.3, 4.4 |
| overcommit | allocating more virtual memory than physical exists | 4.3 |
| OOM killer | kernel kills a process when RAM+swap is exhausted | 4.3 |
| mmap | create a mapping (anon/file, shared/private) | 4.4 |
| mprotect | change a mapping's permissions (page-aligned) | 4.4 |
| madvise | usage hints: DONTNEED, WILLNEED, DONTFORK | 4.4 |
| page cache | kernel's in-RAM cache of file pages (= file-backed mmap) | 4.4 |
| guard page | PROT_NONE page that faults on access (bug detection) | 4.5 |
| shadow memory | compact "is this poisoned?" map; cheap via demand paging | 4.5 |
