# Lesson 5 — Glossary

| Term | One line | § |
|---|---|---|
| stack | region holding call frames + automatic locals; grows down | 5.1 |
| stack pointer (RSP) | register marking the current top of the stack | 5.1 |
| frame | one call's slice: return addr, saved RBP, locals, spills | 5.1, 5.2 |
| LIFO | last-in first-out; matches call nesting | 5.1 |
| RLIMIT_STACK | the stack size limit (~8 MiB here; ulimit -s) | 5.1 |
| ABI | binary contract for calls (SysV AMD64 on Linux x86-64) | 5.2 |
| argument registers | RDI RSI RDX RCX R8 R9 (then stack) | 5.2 |
| return register | RAX | 5.2 |
| callee-saved | RBX RBP R12-R15 RSP: callee must restore | 5.2 |
| caller-saved | RAX RCX RDX RSI RDI R8-R11: caller saves if needed | 5.2 |
| return address | pushed by `call`; where `ret` jumps back to | 5.2, 5.5 |
| frame pointer (RBP) | optional per-frame base; links frames | 5.2, 5.3 |
| 16-byte alignment | RSP alignment required at a call | 5.2 |
| red zone | 128 bytes below RSP usable without adjusting it | 5.2 |
| unwinding | reconstructing the call chain from the stack | 5.3 |
| __builtin_frame_address / return_address | compiler intrinsics for frame/return | 5.3 |
| DWARF CFI | .eh_frame tables enabling unwinding without frame pointers | 5.3 |
| backtrace() | glibc API returning the current return-address chain | 5.3 |
| name mangling | encoding of C++ names in symbols (_Z4showv) | 5.3 |
| addr2line / dladdr | map an address to symbol / file:line | 5.3 |
| stack overflow | exhausting the stack (recursion/big locals) → guard-page fault | 5.4 |
| alloca / VLA | runtime-sized stack allocation; stack-clash risk | 5.4 |
| stack buffer overflow | overrunning a local into saved RBP/return address | 5.5 |
| stack canary | random guard before the return address; aborts on smash | 5.5 |
| ROP | return-oriented programming: chain existing code (defeats NX) | 5.5 |
