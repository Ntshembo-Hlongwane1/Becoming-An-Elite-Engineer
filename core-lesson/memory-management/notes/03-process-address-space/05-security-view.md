# 3.5 — The Security Researcher's View: the Map Is the Board

An exploit is a plan drawn on the address-space map. Each region offers the attacker something
different, and each defence (W^X, ASLR, guards) removes a square from the board. Three hats, applied
to the map itself.

## What each region is "worth"
| Region | To an attacker | Defence that blunts it |
|---|---|---|
| `.text` (r-x) | gadgets for code-reuse (ROP) | ASLR/PIE randomizes its base |
| `.data`/`.bss` (rw-) | overwritable globals, function pointers, GOT entries | RELRO makes the GOT read-only after load |
| heap (rw-) | corrupt a neighbour's object or allocator metadata | your capstone's red zones + quarantine; hardened allocators (L17) |
| stack (rw-, no x) | overwrite a return address | stack canaries; NX stops injected shellcode |
| libraries (r-x) | libc gadgets, `system()` | ASLR of the library base |
| unmapped gaps / guards | nothing — accessing faults | the whole point; guards are deliberate |

## Offense — discovery
- Read `/proc/<pid>/maps` (if you can) to learn the live layout: which regions are `rwx` (jackpot,
  rare), where libc is, how big the stack is. In RE you reconstruct this from the binary + loader
  behaviour.
- Classify a crash address: a fault at a `0x7fff…` address implicates the stack; `0x7f…` the mmap
  region/libraries; a low fixed address a `-no-pie` binary's own segments. This triage is step one
  of turning a crash into a bug report.

## Offense — value, with honest ceilings
- A write you control **into `.data`/GOT or a return address** → control flow → potentially code
  execution (ceiling). ASLR usually forces you to *also* leak an address first, so a single bug is
  often not enough — you chain a leak + a write.
- A read you control → **info leak** (defeats ASLR by revealing a current base) — frequently the
  *first* half of a two-bug chain, not the payload itself. Recognising "this bug is a leak primitive,
  valuable mainly to enable another" is exactly the researcher judgement your methodology is after.

## Defense — impact and construction
- **Keep W^X**: never map a page `rwx`; for JITs, write-then-`mprotect`-to-exec, never both.
- **Keep ASLR**: build PIE (default), so your own `.text`/globals randomize.
- **Guards**: unmapped/`PROT_NONE` pages between regions turn overflows into faults — your capstone
  weaponises this for *detection*.
- The map is also your **triage tool** defensively: a crash dump's fault address + `maps` tells you
  which region was violated, which narrows the bug class before you even open a debugger.

## Through-line
Your capstone reads `maps`-style region knowledge to classify fault addresses and to place its own
guard/`PROT_NONE` pages. When you later red-team your object store, the first thing you'll do with a
crash is locate its address on this map.

## My summary
