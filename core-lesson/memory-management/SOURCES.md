# Sources (shared across the curriculum)

Tags used in the notes. **(derived)** = reasoning you can check; **(measured)** = a program run on
your VM, shown in the notes. Untagged, unmarked claims are bugs — check them.

## Your books (the spine)
| Key | Source |
|---|---|
| `[MEM §x]` | A. Alheraki, *Advanced Memory Management in Modern C++*, 2nd ed., 2025 |
| `[ALGO §x]` | A. Alheraki, *Modern C++ Algorithms*, 2025 |
| `[CIA §x]` | A. Williams, *C++ Concurrency in Action*, 2nd ed., Manning 2019 |
| `[MCCP §x]` | A. Alheraki, *Modern C++ Concurrency and Parallel Programming*, 2026 |
| `[HPC §x]` | A. Alheraki, *The Hidden Power: C++26 in Offensive & Defensive Cybersecurity*, 2026 |

## C++ standard (working draft, eel.is/c++draft) and cppreference
| Key | Source |
|---|---|
| `[STD-intro.memory]` | [intro.memory] — byte, CHAR_BIT, memory location. https://eel.is/c++draft/intro.memory |
| `[STD-basic.fundamental]` | [basic.fundamental] — integer types, widths, modulo 2^N, two's-complement range. https://eel.is/c++draft/basic.fundamental |
| `[STD-conv.integral]` | [conv.integral] — integer→integer is modulo 2^N. https://eel.is/c++draft/conv.integral |
| `[STD-conv.prom]` | [conv.prom] — integral promotion. https://eel.is/c++draft/conv.prom |
| `[STD-expr.arith.conv]` | [expr.arith.conv] — usual arithmetic conversions (signed/unsigned mixing). https://eel.is/c++draft/expr.arith.conv |
| `[STD-expr.shift]` | [expr.shift] — shift semantics; UB if count<0 or ≥ width. https://eel.is/c++draft/expr.shift |
| `[STD-expr.pre]` | [expr.pre]/4 — out-of-range arithmetic result is UB. https://eel.is/c++draft/expr.pre |
| `[CPPREF-types]` | cppreference, Fixed width integer types (`<cstdint>`). https://en.cppreference.com/w/cpp/types/integer |
| `[CPPREF-limits]` | cppreference, `std::numeric_limits`. https://en.cppreference.com/w/cpp/types/numeric_limits |
| `[CPPREF-endian]` | cppreference, `std::endian`. https://en.cppreference.com/w/cpp/types/endian |
| `[CPPREF-byteswap]` | cppreference, `std::byteswap` (C++23). https://en.cppreference.com/w/cpp/numeric/byteswap |
| `[CPPREF-bit_cast]` | cppreference, `std::bit_cast`. https://en.cppreference.com/w/cpp/numeric/bit_cast |

## Toolchain & platform
| Key | Source |
|---|---|
| `[GCC-overflow]` | GCC manual, *Integer Overflow Builtins* (`__builtin_*_overflow`). https://gcc.gnu.org/onlinedocs/gcc/Integer-Overflow-Builtins.html |
| `[GCC-fwrapv]` | GCC manual, `-fwrapv`, `-ftrapv`, `-fsanitize=undefined`. https://gcc.gnu.org/onlinedocs/gcc/Code-Gen-Options.html |
| `[MAN-malloc]` | `malloc(3)`. https://man7.org/linux/man-pages/man3/malloc.3.html |
| `[CERT-INT30]` | CERT C/C++ INT30-C/INT32-C: ensure operations on integers do not wrap / result in overflow. https://wiki.sei.cmu.edu/confluence/display/c/INT30-C.+Ensure+that+unsigned+integer+operations+do+not+wrap |

## CVEs referenced (by number; mechanism derived in-notes, not reproduced as exploits)
| Key | Source |
|---|---|
| `[CVE-2002-0639]` | OpenSSH integer overflow in challenge-response (classic `nresp` multiply overflow). https://nvd.nist.gov/vuln/detail/CVE-2002-0639 |

## Cross-references to already-built lessons
| Key | Source |
|---|---|
| `[LSM]` | `../modern-cpp-algorithms/notes/lsm-tree/` |
| `[ALIGN]` | `../modern-cpp-algorithms/notes/aligned-memory-direct-io/` (Lesson 9) |

## Machine facts (measured on your VM, 2026-10-07)
| Fact | Value | How |
|---|---|---|
| CHAR_BIT | 8 | `m.cpp` |
| sizeof char/short/int/long/longlong/ptr | 1/2/4/8/8/8 | `m.cpp` |
| `int` range | −2147483648 … 2147483647 | `m.cpp` |
| `char` signedness | **signed** (200 → −56) | `m.cpp` |
| byte order | little-endian (`std::endian::native`) | `m.cpp` |
| signed overflow | UB — UBSan reports it; `-fwrapv` makes it wrap | `ov.cpp` |
| unsigned overflow | wraps mod 2³² (defined) | `m.cpp` |
| Compiler / libc | GCC 15.2.0 / glibc 2.43 | — |

## Lesson 7 — How malloc works (allocation)
| Key | Source |
|---|---|
| `[MAN-malloc]` | `malloc(3)` — heap via sbrk, mmap above MMAP_THRESHOLD=128 kB, alignment, free(NULL)/malloc(0), double-free UB. https://man7.org/linux/man-pages/man3/malloc.3.html |
| `[MAN-sbrk]` | `brk(2)`/`sbrk(2)` — program break; raising it allocates; sbrk(0) reads it. https://man7.org/linux/man-pages/man2/sbrk.2.html |
| `[MAN-mallopt]` | `mallopt(3)` — M_MMAP_THRESHOLD, M_TRIM_THRESHOLD (both 128 kB), M_MXFAST. https://man7.org/linux/man-pages/man3/mallopt.3.html |
| `[LEA]` | D. Lea, *A Memory Allocator* (dlmalloc) — boundary tags, bins, best-fit, split, coalesce, wilderness. https://gee.cs.oswego.edu/dl/html/malloc.html |
| `[GLIBC-malloc.c]` | glibc `malloc/malloc.c` — struct malloc_chunk, PREV_INUSE/IS_MMAPPED/NON_MAIN_ARENA, min size & 2*size_t alignment. https://sourceware.org/git/?p=glibc.git;a=blob;f=malloc/malloc.c |
| `[SAFELINK]` | glibc 2.32 Safe-Linking of singly-linked free lists. https://sourceware.org/git/?p=glibc.git;a=commit;h=a1a486d70ebcc47a686ff5846875eacad0940e41 |

## Machine facts (measured on your VM, 2026-10-08, Lesson 7)
| Fact | Value | How |
|---|---|---|
| `alignof(max_align_t)` / malloc alignment | 16 | `m1.cpp` |
| small malloc moves the break? | not necessarily (heap pre-grown at start-up) | `m1.cpp` |
| break growth for ~4 MB of 64-B requests | +5,275,648 B (~5.27 MB) | `m2.cpp` |
| `malloc(100)` usable payload | 104 B | `m1.cpp` |
| two `malloc(24)` stride | 32 B (the chunk size) | `m1.cpp` |
| `malloc(200000)` source | mmap (break unmoved; address ~30 TB from heap) | `m1.cpp` |

## Lesson 8 — new/delete for real (allocation)
| Key | Source |
|---|---|
| `[CPPREF-new]` | cppreference, *new expression* — allocate+construct, placement new, ctor-throw cleanup, array cookie. https://en.cppreference.com/w/cpp/language/new |
| `[CPPREF-opnew]` | cppreference, *operator new* — replaceable family, throwing/nothrow/aligned, whole-program replacement, alignment. https://en.cppreference.com/w/cpp/memory/new/operator_new |
| `[CPPREF-opdelete]` | cppreference, *operator delete* — deallocation family, sized delete, noexcept. https://en.cppreference.com/w/cpp/memory/new/operator_delete |
| `[CPPREF-construct_at]` | cppreference, `std::construct_at` / `std::destroy_at` (C++20). https://en.cppreference.com/w/cpp/memory/construct_at |
| `[CPPREF-replacement]` | cppreference, *replacement functions*. https://en.cppreference.com/w/cpp/language/replacement_function |

## Machine facts (measured on your VM, 2026-10-08, Lesson 8)
| Fact | Value | How |
|---|---|---|
| `__STDCPP_DEFAULT_NEW_ALIGNMENT__` | 16 | `n1.cpp` |
| `new T` order | `operator new` runs BEFORE the constructor | `n1.cpp` |
| `delete p` order | destructor runs BEFORE `operator delete` (sized) | `n1.cpp` |
| array cookie for `new D[5]`, sizeof(D)=1 | requested 13 B → 8-byte cookie holds the count | `n1.cpp` |

## Lesson 10 — Custom allocators: arenas, pools, pmr (allocation)
| Key | Source |
|---|---|
| `[CPPREF-memres]` | cppreference, `std::pmr::memory_resource` — abstract interface; public allocate/deallocate/is_equal forward to private virtual do_*. https://en.cppreference.com/w/cpp/memory/memory_resource |
| `[CPPREF-do_allocate]` | cppreference, `memory_resource::do_allocate` — ≥bytes aligned to power-of-two alignment; throws on failure. https://en.cppreference.com/w/cpp/memory/memory_resource/do_allocate |
| `[CPPREF-polyalloc]` | cppreference, `std::pmr::polymorphic_allocator` — runtime-polymorphic Allocator; non-propagating. https://en.cppreference.com/w/cpp/memory/polymorphic_allocator |
| `[CPPREF-monotonic]` | cppreference, `std::pmr::monotonic_buffer_resource` — releases on destruction; deallocate no-op; initial buffer + upstream. https://en.cppreference.com/w/cpp/memory/monotonic_buffer_resource |
| `[CPPREF-allocator]` | cppreference, *Allocator* named requirement + `std::allocator_traits`. https://en.cppreference.com/w/cpp/named_req/Allocator |

## Machine facts (measured on your VM, 2026-10-08, Lesson 10)
| Fact | Value | How |
|---|---|---|
| arena bump vs malloc/free (1M×32B, bulk release) | ≈50–75× faster (0.6 ms vs 37–43 ms) | `m10b.cpp` |
| pool recycling | free a block → next alloc returns the SAME address | `m10.cpp` |
| `std::pmr::vector` over a stack buffer | element storage lands inside the buffer (no heap) | `m10.cpp` |

## Lesson 11 — RAII, rule of 0/3/5, move semantics, exception safety (ownership)
| Key | Source |
|---|---|
| `[CPPREF-rule]` | cppreference, *The rule of three/five/zero*. https://en.cppreference.com/w/cpp/language/rule_of_three |
| `[CPPREF-move-ctor]` | cppreference, *Move constructors* — steal + valid-but-indeterminate; noexcept & vector. https://en.cppreference.com/w/cpp/language/move_constructor |
| `[CPPREF-move_if_noexcept]` | cppreference, `std::move_if_noexcept` — move vs copy for the strong guarantee. https://en.cppreference.com/w/cpp/utility/move_if_noexcept |
| `[CPPREF-copy-elision]` | cppreference, *Copy elision* / NRVO. https://en.cppreference.com/w/cpp/language/copy_elision |
| `[CORE-C.21]` | C++ Core Guidelines C.21 — define or =delete all copy/move/destructor together. https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#c21 |

## Machine facts (measured on your VM, 2026-10-08, Lesson 11)
| Fact | Value | How |
|---|---|---|
| vector realloc, noexcept-move element | relocated by MOVE (7 moves, 0 copies) | `m11.cpp` |
| vector realloc, throwing-move element | relocated by COPY (7 copies, 0 moves) via move_if_noexcept | `m11.cpp` |
| strong guarantee under a throwing element | vector unchanged (size, data ptr, values intact) | `m11.cpp` |
| `return temp;` vs `return std::move(temp);` | NRVO elides vs forced extra move | `m11.cpp` |

## Lesson 12 — Smart pointers inside (ownership)
| Key | Source |
|---|---|
| `[CPPREF-shared]` | cppreference, `std::shared_ptr` — shared ownership, the control block fields, thread safety. https://en.cppreference.com/w/cpp/memory/shared_ptr |
| `[CPPREF-weak]` | cppreference, `std::weak_ptr` — non-owning, `lock()`, `expired()`, breaking cycles. https://en.cppreference.com/w/cpp/memory/weak_ptr |
| `[CPPREF-unique]` | cppreference, `std::unique_ptr` — exclusive ownership, move-only, custom deleter. https://en.cppreference.com/w/cpp/memory/unique_ptr |
| `[CPPREF-make_shared]` | cppreference, `std::make_shared` — single allocation for object + control block. https://en.cppreference.com/w/cpp/memory/shared_ptr/make_shared |
| `[CPPREF-esft]` | cppreference, `std::enable_shared_from_this`. https://en.cppreference.com/w/cpp/memory/enable_shared_from_this |

## Machine facts (measured on your VM, 2026-10-08, Lesson 12)
| Fact | Value | How |
|---|---|---|
| shared_ptr use_count across a copy's scope | 1 → 2 → 1 | `m12.cpp` |
| orphaned shared_ptr cycle (2 nodes) | leaks both (0 destructors run) | `m12.cpp` |
| same cycle with one weak_ptr edge | 0 leaked (both destructors run) | `m12.cpp` |
| weak_ptr lock()/expired() across expiry | alive: expired=0, lock!=null; after: expired=1, lock=null | `m12.cpp` |

## Lesson 13 — Container memory behaviour (ownership)
| Key | Source |
|---|---|
| `[CPPREF-vector]` | cppreference, `std::vector` — contiguous storage, capacity, iterator-invalidation table. https://en.cppreference.com/w/cpp/container/vector |
| `[CPPREF-push_back]` | cppreference, `std::vector::push_back` — reallocation invalidation, amortised O(1), strong guarantee. https://en.cppreference.com/w/cpp/container/vector/push_back |
| `[CPPREF-reserve]` | cppreference, `std::vector::reserve` / `capacity`. https://en.cppreference.com/w/cpp/container/vector/reserve |
| `[CPPREF-string]` | cppreference, `std::basic_string` — contiguous storage (SSO is implementation-defined). https://en.cppreference.com/w/cpp/string/basic_string |
| `[LLVM-smallvector]` | LLVM Programmer's Manual — `SmallVector<T,N>` inline storage + spill. https://llvm.org/docs/ProgrammersManual.html#llvm-adt-smallvector-h |

## Machine facts (measured on your VM, 2026-10-08, Lesson 13)
| Fact | Value | How |
|---|---|---|
| vector<int> capacity growth | 0→1→2→4→8→16→32 (×2, libstdc++) | `m13.cpp` |
| reallocation moves &v[0] | yes — old pointers dangle | `m13.cpp` |
| sizeof(std::string) / short cap | 32 / 15 | `m13.cpp` |
| short string data() location | inside the object (SSO) | `m13.cpp` |
| heap allocs: short vs long string | 0 vs 1 | `m13.cpp` |

## Lesson 14 — The C++ memory model and atomics (concurrency)
| Key | Source |
|---|---|
| `[CIA §5]` | A. Williams, *C++ Concurrency in Action* 2e, ch5 — data race=UB, modification order, synchronizes-with, happens-before, the six orderings / three models (seq_cst, acquire-release, relaxed). |
| `[CIA §7]` | Williams, ch7 — designing lock-free data structures; "prototype with seq_cst". |
| `[MCCP §2.2]` | Alheraki — cache coherency & false sharing. |
| `[MCCP §5.1.2]` | Alheraki — acquire/release; the release-acquire happens-before pair. |
| `[MCCP §11.1.1]` | Alheraki — lock-free = ≥1 thread makes forward progress in finite steps. |
| `[MCCP §12]` | Alheraki — atomic pitfalls and the ABA problem. |
| `[MEM §15.2]` | Alheraki memory book — atomic operations are indivisible. |
| `[CPPREF-atomic]` | cppreference, `std::atomic` + `std::memory_order`. https://en.cppreference.com/w/cpp/atomic/atomic , https://en.cppreference.com/w/cpp/atomic/memory_order |

## Machine facts (measured on your VM, 2026-10-08, Lesson 14; x86-64, GCC 15.2.0)
| Fact | Value | How |
|---|---|---|
| plain-int flag handoff | ThreadSanitizer: **data race** (UB) | `race.cpp` |
| atomic release/acquire handoff | TSan **clean** (exit 0, no warning) | `norace.cpp` |
| SPSC ring, 1,000,000 ints across 2 threads | correct sum, TSan **clean** | `ring.cpp` |

## Lesson 15 — Thread-local allocation & safe reclamation (concurrency)
| Key | Source |
|---|---|
| `[CIA §7.2.2]` | Williams, *C++ Concurrency in Action* 2e — the reclamation problem; leak vs delete; threads-in-pop counter. |
| `[CIA §7.2.3]` | Williams — hazard pointers (Maged Michael). |
| `[MCCP §11]` | Alheraki — lock-free/wait-free; CAS-loop progress under contention. |
| `[MCCP §12]` | Alheraki — the ABA problem; CAS validates equality not history; version/epoch/stamp tags. |
| `[VYUKOV-mpmc]` | D. Vyukov, *Bounded MPMC queue* — per-cell sequence-number design. https://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue |
| `[CPPREF-cas]` | cppreference, `std::atomic::compare_exchange_weak/strong`. https://en.cppreference.com/w/cpp/atomic/atomic/compare_exchange |

## Machine facts (measured on your VM, 2026-10-08, Lesson 15; x86-64, 4 cores)
| Fact | Value | How |
|---|---|---|
| contended shared atomic vs thread_local (4 threads × 5M) | ~283–358 ms vs ~0.6–0.7 ms (≈400–560×) | `tls.cpp` |
| Vyukov bounded MPMC, 3 producers / 3 consumers, 150k items | correct sum, TSan-clean | `mpmc.cpp` |

## Lesson 16 — How the sanitizers work inside (tools & security)
| Key | Source |
|---|---|
| `[MEM §12.5/§13.1]` | Alheraki memory book — ASan/LSan/MSan/TSan + Valgrind; `-fsanitize` usage and example output. |
| `[ASAN-ALGO]` | Google Sanitizers wiki, *AddressSanitizer Algorithm* — runtime replaces malloc/free, redzones, quarantine, shadow memory (8→1, `Shadow=(Mem>>3)+offset`), shadow byte encoding. https://github.com/google/sanitizers/wiki/AddressSanitizerAlgorithm |
| `[ASAN-CLANG]` | Clang docs, *AddressSanitizer* — bug classes caught; LSan integrated. https://clang.llvm.org/docs/AddressSanitizer.html |
| `[ASAN-PAPER]` | Serebryany et al., *AddressSanitizer: A Fast Address Sanity Checker*, USENIX ATC 2012. |
| `[MAN-backtrace]` | `backtrace(3)`. https://man7.org/linux/man-pages/man3/backtrace.3.html |

## Machine facts (measured on your VM, 2026-10-08, Lesson 16)
| Fact | Value | How |
|---|---|---|
| ASan on a never-freed `new int[10]` | LeakSanitizer: 40 bytes leaked in 1 allocation | `s_leak.cpp` |
| ASan on use-after-free | heap-use-after-free reported at the access | `s_uaf.cpp` |
| ASan on double delete | "attempting double-free" reported | `s_df.cpp` |

## Lesson 17 — The attacker's view & hardening (tools & security)
| Key | Source |
|---|---|
| `[SAFELINK]` | glibc 2.32 Safe-Linking of singly-linked free lists (XOR with pos>>12 + alignment check). https://sourceware.org/git/?p=glibc.git;a=commit;h=a1a486d70ebcc47a686ff5846875eacad0940e41 |
| `[MAN-mprotect]` | `mprotect(2)` — PROT_NONE guard pages (reused from Lesson 4.4). https://man7.org/linux/man-pages/man2/mprotect.2.html |
| `[HARDENED-MALLOC]` | D. Micay, *hardened_malloc* (GrapheneOS). https://github.com/GrapheneOS/hardened_malloc |
| `[PARTITIONALLOC]` | Chromium *PartitionAlloc* — partitioned/type-isolated heap. https://chromium.googlesource.com/chromium/src/+/HEAD/base/allocator/partition_allocator/PartitionAlloc.md |
| `[HPC §4]` | Alheraki, *The Hidden Power: C++26 in Offensive & Defensive Cybersecurity* — heap offense/defense. |

## Machine facts (measured on your VM, 2026-10-08, Lesson 17)
| Fact | Value | How |
|---|---|---|
| guard page (PROT_NONE) past a buffer | 1-byte overflow → SIGSEGV (exit 139), in-bounds writes OK | `guard.cpp` |
| hardened allocator canary on intra-arena overflow | detected by the allocator; **ASan sees nothing** (one arena allocation) | exercise `canary_catches_heap_overflow` |

## Lesson 18 — Fuzzing your own code (applied research, Phase F)
| Key | Source |
|---|---|
| `[LIBFUZZER]` | LLVM, *libFuzzer — coverage-guided fuzz testing*. https://llvm.org/docs/LibFuzzer.html |
| `[SANCOV]` | LLVM, *SanitizerCoverage* (trace-pc/trace-cmp callbacks). https://clang.llvm.org/docs/SanitizerCoverage.html |
| `[AFL]` | AFL / AFL++ — edge-coverage bitmap + mutational fuzzing. https://aflplus.plus/ |
| `[OSSFUZZ]` | Google, *OSS-Fuzz* — continuous fuzzing + sanitizers. https://google.github.io/oss-fuzz/ |
| `[GCC-sancov]` | GCC manual, `-fsanitize-coverage=trace-pc`/`trace-cmp`. https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html |

## Machine facts (measured on your VM, 2026-10-08, Lesson 18; GCC 15.2.0 + ASan)
| Fact | Value | How |
|---|---|---|
| GCC coverage instrumentation | `-fsanitize-coverage=trace-pc` → `__sanitizer_cov_trace_pc` per edge | validated |
| coverage-guided fuzzer vs magic-gated heap overflow | cracks "FUZZ" + crashes in **< 1 s** | exercise |
| coverage-BLIND (random) fuzzer, same target | **20,000,000 iters, no crash** (~1 in 2³²) | exercise |
| reproducer dumped via | `__sanitizer_set_death_callback` (no per-iter I/O) | exercise |
