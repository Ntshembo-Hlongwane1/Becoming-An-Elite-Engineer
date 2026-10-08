# Lesson 15 — Glossary

| Term | One line | § |
|---|---|---|
| contention | many cores hammering one atomic/cache line → serialised, slow | 15.1 |
| thread-local state | per-thread data; uncontended fast path (≈400× here) | 15.1 |
| `thread_local` | one instance per thread; avoids sharing (not a sync tool) | 15.1 |
| per-thread allocator cache | tcache/pools: most malloc/free never touch shared state | 15.1 |
| reclamation problem | when is it safe to free a node another thread may read? | 15.2 |
| Treiber stack | lock-free stack via atomic head + CAS; the reclamation hazard | 15.2 |
| use-after-free (lock-free) | free a node a concurrent reader still dereferences | 15.2 |
| threads-in-pop counter | free the pending list only when no thread is in pop | 15.2 |
| ABA problem | value goes A→B→A; CAS succeeds on a logically different state | 15.3 |
| CAS checks equality not history | why ABA fools compare_exchange | 15.3 |
| address reuse | allocators recycle addresses → enables ABA | 15.3 |
| tagged/versioned pointer | CAS {pointer, version} so A→B→A isn't "equal" | 15.3 |
| reference counting (nodes) | atomic refcount per node; correct, contention-heavy | 15.4 |
| hazard pointer | thread advertises a node it's using; defer freeing it | 15.4 |
| epoch-based reclamation / RCU | free a retired node only after a grace period | 15.4 |
| grace period | interval after which no thread can hold an old reference | 15.4 |
| bounded MPMC queue | fixed array, CAS positions, per-cell sequence numbers | 15.4 |
| cell sequence number | per-cell tag advancing each lap → MPMC-correct, ABA-free | 15.4 |
| no-reclamation design | array reused in place; no free → no lifetime question | 15.4 |
| CAS loop | read, compute, compare_exchange, retry on failure | 15.4 |
| not-a-data-race bug | ABA/reclamation: TSan can't see them; reason/review/stress | 15.5 |
| unbounded deferral DoS | reclamation deferred forever under load/stall → RSS grows | 15.5 |
