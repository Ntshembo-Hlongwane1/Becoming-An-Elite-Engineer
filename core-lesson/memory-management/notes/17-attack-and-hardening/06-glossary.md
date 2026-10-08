# Lesson 17 — Glossary

| Term | One line | § |
|---|---|---|
| heap exploitation pipeline | trigger → groom → corrupt → write-what-where → hijack | 17.1 |
| heap grooming / feng shui | arranging allocations so the corruptible chunk sits by the target | 17.1 |
| metadata corruption | overflow into a chunk's size/flags/free-list pointer | 17.1 |
| fastbin/tcache poisoning | overwrite a freed chunk's next → malloc returns an attacker address | 17.1/17.3 |
| write-what-where | the arbitrary-write primitive exploitation aims for | 17.1 |
| honest ceiling | what a bug gives vs what still gates escalation (leak, grooming) | 17.1/17.5 |
| heap canary | random, per-block, secret-derived value around the payload | 17.2 |
| header/trailer canary | canary before/after payload → catches overflow either way | 17.2 |
| per-block secret canary | mixed with the block address so one leak can't forge another | 17.2 |
| poison-on-free | overwrite freed bytes (0xDD) — clear secrets, surface UAF | 17.2 |
| double-free magic | ALLOC/FREE state word; re-free detected (glibc tcache key) | 17.2 |
| detection vs prevention | tripwire at free vs fault/impossible-by-construction | 17.2/17.5 |
| safe-linking | store free-list next ⊕ (slot>>12); glibc 2.32 | 17.3 |
| alignment/size sanity checks | reject forged/garbage-decoded pointers and sizes | 17.3 |
| out-of-line metadata | bookkeeping in a separate region an overflow can't reach | 17.3 |
| guard page | PROT_NONE page; overflow faults in hardware (MMU) | 17.4 |
| NX / W^X | non-executable heap/stack → forces ROP | 17.4 |
| ASLR / PIE | randomized layout → attacker needs an info leak | 17.4 |
| isolation / partitioning | per-type heaps; cross-type corruption impossible | 17.4 |
| hardened_malloc / PartitionAlloc | production security allocators (out-of-line meta, guards, isolation) | 17.3/17.4 |
| quarantine | withhold freed memory from reuse → catch UAF (ASan/hardened) | 17.4 |
| defense in depth | layered mitigations; attacker must defeat all | 17.4/17.5 |
| build→break→defend | the full loop; the researcher capability | 17.5 |
