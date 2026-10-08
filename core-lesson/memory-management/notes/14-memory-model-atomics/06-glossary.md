# Lesson 14 — Glossary

| Term | One line | § |
|---|---|---|
| memory model | the rules for when one thread sees another's memory writes | 14.1 |
| reordering | compiler/CPU executing memory ops out of source order | 14.1 |
| cache visibility | a core's write may not yet be visible to other cores | 14.1 |
| data race | unordered accesses to one location, ≥1 write, ≥1 non-atomic → UB | 14.1 |
| modification order | the per-object total order of writes all threads agree on | 14.1 |
| atomic operation | indivisible op; no thread sees it half-done | 14.2 |
| `std::atomic<T>` | wraps a trivially-copyable T with atomic ops + ordering | 14.2 |
| torn read/write | a non-atomic multi-word update seen half-old/half-new | 14.2/14.5 |
| `is_lock_free()` | whether an atomic uses hardware (not an internal lock) | 14.2 |
| lock-free | ≥1 thread always makes progress; atomics + retries, no mutex | 14.2/14.4 |
| wait-free | every thread finishes in bounded steps (SPSC push/pop) | 14.4 |
| sequenced-before | single-thread source order → happens-before | 14.3 |
| synchronizes-with | release store ↔ acquire load that reads it (cross-thread) | 14.3 |
| happens-before | if A hb B then B sees A's effects; the goal | 14.3 |
| memory_order_seq_cst | default; a single total order all threads agree on | 14.3 |
| acquire-release | pairwise publish/observe; no global total order | 14.3 |
| release store | one-way barrier: prior writes visible to an acquirer | 14.3/14.4 |
| acquire load | one-way barrier: sees the releaser's prior writes | 14.3/14.4 |
| memory_order_relaxed | atomicity only; no cross-variable ordering | 14.3 |
| SPSC | single producer, single consumer (one writer per index) | 14.4 |
| ring buffer | fixed array + head/tail indices, wraparound | 14.4 |
| one-empty-slot | reserve a slot so full ≠ empty; usable cap = Cap−1 | 14.4 |
| false sharing | head_/tail_ on one line → cache ping-pong; fix alignas(64) | 14.4 |
| TOCTOU | check-then-act race: state changes in the gap → UAF/OOB | 14.5 |
| ABA problem | CAS sees value return to A; object freed+reused between → UAF | 14.5 |
| ThreadSanitizer (TSan) | tool that reports data races via happens-before | 14.5 |
| volatile ≠ atomic | volatile gives neither atomicity nor ordering | 14.5 |
