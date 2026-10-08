# Lesson 16 — How the Sanitizers Work Inside (Phase E)

> Status: complete. Exercise: `../../exercises/16-leak-detector/`.
>
> Every lesson so far has leaned on AddressSanitizer and ThreadSanitizer to *prove* the exercises
> correct. This lesson opens the black box: **how do these tools actually catch a use-after-free, a
> buffer overflow, a leak — at runtime, on your code?** The answer is a combination of two things you
> already know how to build: **intercepting the allocator** (Lesson 8's `operator new`/`malloc` hook)
> and keeping **metadata about every allocation** (Lesson 7's chunk header, Lesson 16's side table).
> ASan adds a third idea — **shadow memory** — that lets it check *every memory access* in O(1).
> Understanding this turns the tools from magic into mechanism, tells you what they can and cannot
> catch, and is the direct blueprint for your capstone detector. You'll build the simplest, most
> useful piece yourself: a **mini leak detector** that hooks allocation, records every live block, and
> reports what leaked — a hand-rolled LeakSanitizer.

This is Phase E, Lesson 1 (tools & security). It builds on Lesson 8 (allocator interception), Lesson 7
(allocation metadata), and the sanitizer use throughout Phases A–D.

## Ground rules recap
Every claim is quoted or marked **(derived)** / **(measured)** (run on your VM — GCC 15.2.0). New
sources at the bottom and in `SOURCES.md`.

## Read in order
1. `01-interception-and-metadata.md` — the two primitives every memory tool is built on: replace the
   allocator (Lesson 8) and keep per-allocation metadata (Lesson 7); the recursion/early-init hazards
   a real interceptor must dodge.
2. `02-how-asan-works.md` — ASan internals: the runtime replaces `malloc`/`free`, **redzones** around
   allocations, a **quarantine** for freed memory (→ UAF), and **shadow memory** (1 byte per 8,
   `Shadow=(Addr>>3)+off`) checked on every access. What each shadow value means. Measured: ASan
   catching a UAF, an overflow, a double-free.
3. `03-leaks-and-reachability.md` — LeakSanitizer: why "still live at exit" needs **reachability**
   (a mark-sweep from roots) to avoid false positives; what "definitely lost" vs "still reachable"
   mean; our simpler explicit-tracking approach. Measured: LSan's leak report.
4. `04-building-a-leak-detector.md` — the design you implement: a side table mapping live pointer →
   `{size, id}`, `track`/`untrack` on the allocation hooks, double-/invalid-free detection, and a
   report — with fixed storage so the detector never allocates through the thing it's hooking.
5. `05-security-view.md` — three hats: sanitizers as the defender's microscope and the attacker's recon
   tool; their blind spots; what to run in CI.
6. `06-glossary.md`.

Then do the exercise: **a mini leak detector** — implement a `LeakTracker` that records every
allocation made through its hooks, detects double/invalid frees, and reports the blocks still live
(the "leaks"), using fixed side storage. Tested under ASan+UBSan.

## The one-paragraph picture
A memory tool needs to see every allocation and (for ASan) every access. It sees **allocations** by
**replacing `malloc`/`free`** (and `operator new`/`delete`) with its own versions — the interception
you built in Lesson 8 — and recording **metadata** about each block. A pure **leak detector**
(LeakSanitizer, and your exercise) keeps a table of live blocks: insert on alloc, erase on free; at the
end, whatever's left (and unreachable) leaked. **AddressSanitizer** goes further: its `malloc`
surrounds each block with poisoned **redzones**, its `free` drops the block into a **quarantine** so
the address isn't reused (catching use-after-free), and it keeps **shadow memory** — one byte
describing the addressability of every eight application bytes — that the **compiler-instrumented
code** consults before *every* load and store, so an out-of-bounds or use-after-free access is caught
at the instruction that commits it `[ASAN-ALGO]`. The cost is memory (shadow + redzones) and ~2× time;
the payoff is precise, immediate detection with the faulting *and* allocation stacks. Your mini
detector implements the leak-tracking half with a fixed side table (so it never recurses into the
allocator it hooks), and reports exactly what a hand-rolled LeakSanitizer would.

## New sources introduced here (also appended to `SOURCES.md`)
| Key | Source |
|---|---|
| `[MEM §12.5/§13.1]` | Alheraki memory book — memory sanitizers (ASan, LSan, MSan, TSan), Valgrind; `-fsanitize` usage and example output. |
| `[ASAN-ALGO]` | Google Sanitizers wiki, *AddressSanitizer Algorithm* — runtime replaces malloc/free, redzones, quarantine, shadow memory (8→1, `Shadow=(Mem>>3)+offset`), shadow byte encoding. https://github.com/google/sanitizers/wiki/AddressSanitizerAlgorithm |
| `[ASAN-CLANG]` | Clang docs, *AddressSanitizer* — detects OOB (heap/stack/global), use-after-free/return/scope, double/invalid free, leaks; LSan integrated. https://clang.llvm.org/docs/AddressSanitizer.html |
| `[ASAN-PAPER]` | Serebryany, Bruening, Potapenko, Vyukov, *AddressSanitizer: A Fast Address Sanity Checker*, USENIX ATC 2012. |
| `[MAN-backtrace]` | `backtrace(3)` — capturing a call stack for allocation-site reporting. https://man7.org/linux/man-pages/man3/backtrace.3.html |
