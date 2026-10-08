# Lesson 16 — Glossary

| Term | One line | § |
|---|---|---|
| interception | replacing malloc/free (or operator new/delete) to see every allocation | 16.1 |
| LD_PRELOAD shim | a preloaded .so defining malloc/free to intercept without recompiling | 16.1 |
| dynamic binary instrumentation | run the program on a synthetic CPU (Valgrind); 10–50× | 16.1 |
| per-allocation metadata | size/site/liveness recorded per block; decides what you can detect | 16.1 |
| inline vs out-of-line metadata | next to data (fast, corruptible) vs side table (safe) | 16.1 |
| interceptor recursion | a hook that allocates re-enters itself; fix with fixed storage | 16.1 |
| AddressSanitizer (ASan) | runtime + instrumentation detecting OOB/UAF/double-free/leaks | 16.2 |
| redzone | poisoned guard region around an allocation → overflow detection | 16.2 |
| quarantine | freed memory withheld from reuse → use-after-free detection | 16.2 |
| shadow memory | 1 byte per 8 app bytes; `Shadow=(Addr>>3)+off` | 16.2 |
| shadow byte encoding | 0=all addressable, k=first k ok, negative=all poisoned | 16.2 |
| instrumented access | compiler-inserted shadow check before every load/store | 16.2 |
| ASan cost | ~2× CPU + large memory; a test tool, not production | 16.2 |
| MSan / TSan | uninitialized-read detector / data-race detector (separate builds) | 16.2 |
| LeakSanitizer (LSan) | leak detector; tracks alloc/free, reports at exit | 16.3 |
| reachability analysis | GC-style mark-sweep from roots to classify leaks | 16.3 |
| definitely lost | no pointer to the block exists → a real leak | 16.3 |
| still reachable | unfreed but a pointer still exists (usually benign) | 16.3 |
| leak tracker (ours) | side table: track on alloc, untrack on free, report live | 16.3/16.4 |
| double-free | untrack a pointer already removed | 16.3 |
| invalid-free | untrack a pointer never tracked | 16.3 |
| allocation-site backtrace | `backtrace(3)` stored per block → "allocated here" | 16.1/16.4 |
| sanitizer blind spots | ABA, shared_ptr cycles, custom-allocator internals, TOCTOU | 16.5 |
