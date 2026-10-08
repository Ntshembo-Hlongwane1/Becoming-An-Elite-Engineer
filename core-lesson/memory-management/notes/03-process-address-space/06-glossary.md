# Lesson 3 — Glossary

| Term | One line | § |
|---|---|---|
| address space | the flat per-process array of virtual bytes (0 .. 2^48-1 on x86-64) | 3.0 |
| segment | a logical region: .text/.rodata/.data/.bss/heap/stack | 3.1 |
| `.text` | machine code; mapped r-x (not writable) | 3.1 |
| `.rodata` | read-only data (literals, const); r-- | 3.1 |
| `.data` | initialized globals/statics; rw-; stored in the ELF file | 3.1 |
| `.bss` | zero-initialized globals; rw-; only a size on disk, zero-filled at load | 3.1 |
| ELF | the executable/library file format on Linux | 3.1 |
| VMA | virtual memory area: one contiguous mapping with uniform perms | 3.2 |
| `/proc/pid/maps` | kernel's list of a process's VMAs (address perms offset dev inode path) | 3.2 |
| perms `rwxp/s` | read/write/execute; private(COW) or shared | 3.2 |
| inode 0 | anonymous mapping, no backing file (heap/stack/bss/anon mmap) | 3.2 |
| pseudo-path | `[heap]`, `[stack]`, `[vdso]`, `[vvar]`, `[vsyscall]` | 3.2 |
| vdso | kernel-provided page of fast user-space syscalls | 3.2 |
| heap | malloc/new region; grows toward higher addresses (brk/sbrk) | 3.3 |
| stack | locals/return addresses; grows toward lower addresses; ~8 MiB default | 3.3 |
| mmap region | libraries + anonymous mmaps + large mallocs, between heap and stack | 3.3 |
| guard page | deliberately unmapped/PROT_NONE sliver; access faults | 3.3, 3.4 |
| zero page | low addresses left unmapped so nullptr deref faults | 3.3 |
| ASLR | randomizes region base addresses each run | 3.4 |
| PIE | position-independent executable; lets ASLR randomize your own code | 3.4 |
| W^X / NX | no page is both writable and executable | 3.4 |
| mprotect | syscall to change a region's permissions at run time | 3.4 |
| ROP | return-oriented programming: reuse existing code instead of injecting | 3.4, 3.5 |
| RELRO | makes the GOT read-only after load | 3.5 |
