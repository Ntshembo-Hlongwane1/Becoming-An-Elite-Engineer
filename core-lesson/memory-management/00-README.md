# Memory Management in Modern C++ — A Build-Up Curriculum From Zero (Systems / Security Edition)

> Goal: make memory management **second nature**. By the end you can follow a byte from a C++
> object, through the allocator, through the kernel, to the cache line and the DRAM chip — and
> back — and reason about every bit on the way, including how each layer is attacked and defended.

This is a full course, not a single lesson. It is built so that **each lesson only uses what
earlier lessons taught** — no forward references, no assumed knowledge. Every lesson ships with an
**exercise that has real tests and a `run.sh`**: you implement stubs until `./run.sh` prints
`ALL TESTS PASSED`.

## Ground rules (every lesson obeys these)

1. **Zero assumed knowledge of concepts.** You can write C++; nothing else is assumed. Every term
   is defined before use.
2. **Nothing from thin air.** Every claim is either quoted from a cited source, or marked
   **(derived)** (reasoning you can check) or **(measured)** (a program I ran on your VM, shown so
   you can re-run it). Sources are in `SOURCES.md`.
3. **Your books are the spine.** `[MEM §x]` = *Advanced Memory Management in Modern C++* (Alheraki),
   `[ALGO §x]` = *Modern C++ Algorithms* (Alheraki), `[CIA §x]` = *C++ Concurrency in Action, 2nd ed.*
   (Williams), `[MCCP §x]` = *Modern C++ Concurrency and Parallel Programming* (Alheraki),
   `[HPC §x]` = *The Hidden Power: C++26 in Offensive & Defensive Cybersecurity* (Alheraki). Where a
   book example is wrong or unsafe, I show the compiler/sanitizer output that proves it — spotting
   that is part of the training.
4. **Line-by-line code.** If a line has no explanation, that's a bug in the notes.
5. **Security is not a bolt-on.** Every lesson ends with a researcher's view: how the thing is
   discovered, what it gives an attacker (with an honest ceiling), and the defence that makes it
   impossible by construction. Framing follows the "three hats" habit from the LSM/alignment lessons.

## The map

Each lesson = a `notes/NN-topic/` folder + an `exercises/NN-topic/` project with tests.

### Phase A — The machine (what memory *is*)
| # | Lesson | You will build (exercise) |
|---|---|---|
| 1 | Bits, bytes & integer representation | overflow-checked integer arithmetic + endian codec |
| 2 | Objects, pointers & lifetime in C++ | a safe byte reader/writer over raw buffers |
| 3 | A process's address space | an `/proc/self/maps` parser that classifies any address |
| 4 | Virtual memory: pages, faults, COW | a page-fault & lazy-allocation prober |
| 5 | The stack & the calling convention | a stack-depth prober + frame reasoning |
| 6 | Caches & the memory hierarchy | cache-aware transforms + a timing harness |

### Phase B — Allocation (who hands out memory)
| # | Lesson | You will build |
|---|---|---|
| 7 | How `malloc` works (brk/mmap, chunks, bins) *(done)* | your own free-list allocator (split + coalesce) |
| 8 | `new`/`delete` for real (placement new, replacement) *(done)* | an allocation-counting global `operator new` |
| 9 | **Alignment & direct I/O** *(already built — links to `../modern-cpp-algorithms/.../aligned-memory-direct-io`)* | (6 exercises, done) |
| 10 | Custom allocators: arenas, pools, `std::pmr` *(done)* | a pool allocator + a `pmr::memory_resource` |

### Phase C — Ownership & lifetime
| # | Lesson | You will build |
|---|---|---|
| 11 | RAII, rule of 0/3/5, move semantics, exception safety *(done)* | a `Vector<T>` with the strong guarantee |
| 12 | Smart pointers inside *(done)* | your own `UniquePtr`, `SharedPtr`, `WeakPtr` |
| 13 | Container memory behaviour (growth, SSO, invalidation) *(done)* | a `SmallVector<T,N>` with inline storage |

### Phase D — Concurrency
| # | Lesson | You will build |
|---|---|---|
| 14 | The C++ memory model & atomics *(done)* | a lock-free SPSC ring buffer (TSan-clean) |
| 15 | Thread-local allocation & safe reclamation *(done)* | a bounded lock-free MPMC queue |

### Phase E — Tools & security
| # | Lesson | You will build |
|---|---|---|
| 16 | How the sanitizers work inside *(done)* | a mini leak detector hooking `new`/`malloc` |
| 17 | The attacker's view of memory & hardening *(done)* | harden a free-list allocator (canaries, safe-linking, guard pages) |

### Phase F — Applied memory-bug research (the elite bridge: find → reproduce → defend, on your own code)
| # | Lesson | You will build |
|---|---|---|
| 18 | Fuzzing your own code *(done)* | a minimal coverage-guided fuzzer that finds a magic-gated heap overflow |
| 19 | From bug to primitive (controlled exploitation, local lab) | a working PoC against your Lesson-7 allocator, defeated by Lesson-17 hardening |
| 20 | Reading the machine (RE on-ramp) | find a bug in a stripped binary of your own code (gdb/objdump/Ghidra) |
| 21 | The capstone detector + a real finding | assemble 8+16+17 into a heap detector (link-in **and** `LD_PRELOAD`); writeup |
| 22 | Weak memory & microarchitecture | atomics on ARM under qemu; defensive cache-timing side channels |

## How to study a lesson

1. Read `notes/NN-topic/` straight through once for the story.
2. Re-read with `SOURCES.md` open; re-run at least one **(measured)** program yourself.
3. Do the exercise: `cd exercises/NN-topic && ./run.sh`. Replace every `Todo(...)` until it is
   green **under sanitizers**.
4. Write a 5-line summary in your own words under `## My summary` at the end of each notes file,
   and one sourced line per decision in the exercise's `DECISIONS.md`.
5. Only then read the matching production code the lesson points to, and record the differences.

## Status

- Lessons 1–8 + 10–16 are complete (`notes/`, `exercises/`). Lesson 7 = your own free-list
  allocator (split + coalesce); Lesson 8 = a counting global `operator new`; Lesson 10 = arena + pool + a `pmr::memory_resource`; Lesson 11 = a `Vector<T>` with the strong guarantee; Lesson 12 = your own `UniquePtr`/`SharedPtr`/`WeakPtr`; Lesson 13 = a `SmallVector<T,N>` with inline storage (all under ASan+UBSan); Lesson 14 = a lock-free SPSC ring buffer and Lesson 15 = a bounded lock-free MPMC queue (both TSan-clean). Phases A–E COMPLETE. Lesson 16 = a mini leak detector; Lesson 17 = a hardened allocator (canaries + safe-linking). The full core curriculum (1–17) is built. **Phase F (applied research, 18–22) is underway:** Lesson 18 = a coverage-guided fuzzer that finds a magic-gated heap overflow (built on GCC `-fsanitize-coverage` + ASan; measured <1s vs 20M-iter random miss). Next: 19 exploitation PoC, 20 RE on-ramp, 21 capstone detector, 22 weak memory. (Lesson 9 = alignment, already done.)
- Lesson 9 (alignment & direct I/O) is complete under
  `../modern-cpp-algorithms/notes/aligned-memory-direct-io/` + its exercises.
- Other lessons are built in order. Each `notes/NN-*/00-README.md` says whether it is complete.
