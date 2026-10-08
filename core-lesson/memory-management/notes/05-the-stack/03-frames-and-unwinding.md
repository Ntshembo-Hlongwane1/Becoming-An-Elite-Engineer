# 5.3 — Frames and Stack Unwinding

"Unwinding" is reconstructing the chain of active calls from the raw stack — turning bytes into
`main → a → b → c`. Debuggers do it, exception handling does it, and your **capstone** does it to say
"this block was allocated at …". Three ways, increasingly robust.

## 1. The compiler's view: __builtin_frame_address / __builtin_return_address
GCC/Clang expose the current frame and return address:
```cpp
void* fp = __builtin_frame_address(0);   // this frame's base
void* ra = __builtin_return_address(0);  // where this function returns to
```
**(measured)** in the §5.1 program: `builtin_frame_address(0)=0x7ffdee3600e0`,
`return_address=0x7ebf17c2a601` (an address inside libc — `main`'s caller). Level `1` walks one frame
up, etc. These work only when the information is available (frame pointers or CFI) and are mostly for
diagnostics.

## 2. The frame-pointer chain
When frame pointers are kept (§5.2 §5), each frame stores the caller's `RBP`, so you can walk:
`rbp -> [saved rbp] -> [saved rbp] -> …`, reading the return address stored just above each saved
RBP. Simple, but **fragile**: it breaks the instant any frame omitted its frame pointer
(`-O2` default), so production unwinders don't rely on it alone.

## 3. DWARF CFI — the robust way
Compilers emit **Call Frame Information** (in the `.eh_frame`/`.debug_frame` ELF sections): a table
that, for every instruction address, says how to recover the caller's registers and return address —
*without* needing a frame pointer. This is what exception unwinding, `gdb`, `libunwind`, and glibc's
`backtrace()` use. It's why unwinding works even in optimized, frame-pointer-less code. The cost is
the metadata tables; the benefit is reliable backtraces anywhere.

## 4. backtrace() — the practical API
glibc's `<execinfo.h>` gives a ready unwinder:
```cpp
void* bt[64];
int n = backtrace(bt, 64);                 // fill bt[] with return addresses, innermost first
char** names = backtrace_symbols(bt, n);   // symbolize (needs -rdynamic / symbol table)
```
**(measured)**, built `-g -rdynamic`:
```
  #0 ./bt(_Z4showv+0x32) ...     show()
  #1 ./bt(_Z4deepv+0xd) ...      deep()
  #2 ./bt(main+0xd) ...          main
  #3 /usr/.../libc.so.6(+0x2a601) ...
  #5 ./bt(_start+0x25) ...
```
The names are **mangled** (`_Z4showv` = `show()`); `c++filt` or `abi::__cxa_demangle` demangles them.
Addresses are returned even without `-rdynamic`; only the *names* need symbols. Your capstone will
`backtrace()` at each allocation and each error, store the frames, and symbolize on report — exactly
this API (or `libunwind` for more control).

## 5. From address to symbol (and source line)
A raw return address becomes meaningful via:
- `backtrace_symbols` / `dladdr` → nearest exported symbol + offset,
- `addr2line -e <binary> <addr>` or the DWARF line table → file:line,
- subtracting the module's load base (ASLR, Lesson 3.4) first, since addresses are randomized.
This "address → function → file:line" pipeline is the same one you'll stretch when you work on
stripped binaries (capstone extension track): with symbols stripped, step 2 gets much harder, which
is the point of that exercise.

## Drills
1. Use `backtrace`/`backtrace_symbols` from three nesting levels; confirm the frame count grows with
   depth. Demangle the names with `abi::__cxa_demangle`.
2. Build `-O2 -fomit-frame-pointer` and confirm `backtrace()` *still* works (CFI), then reason why a
   naive RBP-chain walk would not.
3. Take a return address from `backtrace`, subtract the module base (from `/proc/self/maps`,
   Lesson 3), and `addr2line` it to a source line.
4. Why are the symbol names mangled, and what information does the mangling encode?

## My summary
