# 3.1 — The Segments

When the kernel loads your program it carves the address space into regions, each from a part of the
executable file (an **ELF** binary on Linux) or created empty. These are the classic "segments".

## 1. The measured layout

This program prints the address of one symbol from each segment. Built `-no-pie` so addresses are
fixed (ASLR off, §3.4), on your VM **(measured)**:

```cpp
int g_init = 42;          // initialized global
int g_bss;                // zero-initialized global
const char* lit = "hi";   // "hi" is read-only; lit is a writable pointer
void func(){}             // code
int main(){ int local=1; int* heap=(int*)malloc(16); static int s_local=7; /* print &each */ }
```
```
code  &func      = 0x401196      ┐ .text    (read + execute)
code  &main      = 0x4011a1      ┘
rodata "hi"      = 0x402004        .rodata  (read only)
data  &g_init    = 0x404030      ┐ .data    (read + write, from file)
data  &s_local   = 0x404034      ┘
bss   &g_bss     = 0x404044        .bss     (read + write, zero-filled)
heap  malloc     = 0x17f74010      heap     (read + write, grows up)
stack &local     = 0x7ffd250899cc  stack    (read + write, grows down)
```

Read the addresses: they increase in exactly the textbook order **(derived from the measured
values)** — code `0x401xxx` < rodata `0x402xxx` < data/bss `0x404xxx` < heap `0x17f7xxxx` < stack
`0x7ffdxxxxxxxx`. That ordering is the map you must carry in your head.

## 2. What each segment is

| Segment | Holds | Permissions | Storage duration (L2) | Lifetime |
|---|---|---|---|---|
| `.text` | machine code of your functions | r-x (read, execute, **not** write) | — | whole program |
| `.rodata` | string literals, `const` data | r-- (read only) | static | whole program |
| `.data` | globals/`static`s with a nonzero initial value | rw- | static | whole program |
| `.bss` | globals/`static`s that start at 0 | rw- | static | whole program |
| heap | `malloc`/`new` allocations | rw- | dynamic | until freed |
| stack | locals, return addresses, saved registers | rw- | automatic | per function call |

Two subtleties you can see in the measurements:
- **`.data` vs `.bss`.** Both are read-write globals. The difference is on *disk*: `.data` values are
  stored in the ELF file (the file literally contains the `42`), while `.bss` is just "reserve N
  zeroed bytes" — the file stores only the size, and the kernel zero-fills the pages at load. That's
  why `g_bss` needs no file bytes but `g_init` does. (The maps `inode 0` for anonymous/bss regions in
  the next section reflects this.) It also means a huge zero-initialized global costs nothing on disk
  but real memory at run time.
- **`lit` vs `"hi"`.** The *string* `"hi"` sits in `.rodata` (read-only: writing to a string literal
  is UB and faults). The *pointer* `lit` sits in `.data` (writable). `const char* lit` = "writable
  pointer to const chars", exactly §2.2 §6.

## 3. Why `.text` is not writable and `.rodata` is not executable

Code is mapped **r-x** and data **rw-** — never **rwx** (§3.4 measures this: no writable+executable
region exists). This is the hardware enforcing a rule: you can run code or you can modify data, but
no single region lets you write bytes and then execute them. That is the **W^X / NX** defence, and
it's why classic "inject shellcode into a buffer and jump to it" doesn't work on a modern stack
(the stack is **rw-**, no execute). Attackers answer with reuse techniques (ROP) that chain existing
executable bytes instead — Lesson 17.

## 4. These are *virtual* addresses

`0x404030` is not a RAM chip location; it's an index into *this process's* private virtual address
space (Lesson 4). Another process can have a totally different `g_init` at a totally different
address, and with ASLR on (§3.4) *this* process gets different addresses every run. The segments are
a logical map; Lesson 4 is how the hardware backs it with physical RAM a page at a time.

## Drills
1. Build the §1 program `-no-pie` and reproduce the ordering. Then add a 1-MiB `static char
   big[1<<20];` (zero-initialized) and a 1-MiB `static char big2[1<<20] = {1};` (one nonzero byte).
   Compare the ELF file sizes (`ls -l`) and explain using `.bss` vs `.data`.
2. Try to write through a `char*` aimed at a string literal (`char* p = (char*)"hi"; p[0]='H';`).
   What happens, and which permission bit is responsible?
3. Predict whether `&func < &g_init < &local` on your machine; verify. Which comparisons are
   meaningful and which are technically unspecified across different objects (L2.3)?

## My summary
