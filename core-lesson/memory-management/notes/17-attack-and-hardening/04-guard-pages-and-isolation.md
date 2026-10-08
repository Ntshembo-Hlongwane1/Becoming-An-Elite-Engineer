# 17.4 — Guard pages, and isolation

Canaries and safe-linking (§17.2–17.3) *detect* corruption at `free`. Guard pages and isolation go
further: they make whole classes of corruption **impossible or immediately fatal**, in hardware or by
layout — the strongest kind of defence.

## 1. Guard pages: turn overflow into a hardware fault

A **guard page** is a page mapped `PROT_NONE` (Lesson 4.4 `mprotect`) placed immediately after (and/or
before) a sensitive region. Any access that steps into it — a one-byte linear overflow — triggers a
**page fault → SIGSEGV** at the instruction that does it. No corruption happens: the write never lands.

### (measured) a guard page stops the overflow dead
A 16-byte buffer placed to end exactly at a `PROT_NONE` page, on your VM:
```
writing within buffer... ok
writing 1 byte past (into guard page)... Segmentation fault (exit 139)
```
`(measured; guard.cpp)` The in-bounds writes succeed; the single overflowing byte faults immediately.
Unlike a canary (checked later, at `free`), a guard page catches the overflow *at the moment it
happens*, and it cannot be forged — it's enforced by the MMU (Lesson 4.1).

The trade-off is granularity and cost: a guard page costs a whole page (4 KiB) of address space and a
page-table entry per guarded region, and it only catches overflow that reaches the *page* boundary (so
allocators place small objects adjacent to the guard, or use it for large/sensitive allocations). ASan
(Lesson 16.2) gets byte-granularity instead via shadow memory; guard pages get hardware enforcement and
zero per-access cost. hardened_malloc and PartitionAlloc use guard pages around metadata and large
allocations `[HARDENED-MALLOC, PARTITIONALLOC]`; your capstone detector uses them around tracked
allocations.

## 2. The other hardware/layout defences you've met
- **NX / W^X** (Lesson 3.4): the heap and stack are non-executable, so injected shellcode can't run —
  forcing the attacker to ROP (reuse existing code), which needs an info leak to locate gadgets.
- **ASLR / PIE** (Lesson 3.4): randomizes where the heap, libraries, and stack land, so the attacker
  doesn't know the addresses to point at — the reason safe-linking's `>>12` randomness works, and the
  reason most heap bugs now need a companion **info leak**.
- **Pointer authentication / CFI** (platform-dependent): sign pointers / check indirect-call targets,
  blunting vtable/function-pointer hijacks (§17.1 §3 stage 5).

## 3. Isolation: remove the primitive by construction

The most powerful allocator defence is **partitioning**: put different *kinds* of object in separate
heaps so a bug in one can't reach the other. If attacker-influenced buffers live in their own
partition, far from pointers, vtables, and allocator metadata, then an overflow there corrupts only
same-kind data — never a return address or a free-list pointer. This is "impossible by construction,"
not "detected":
- **Chromium PartitionAlloc** `[PARTITIONALLOC]`: separate partitions per allocator/type; guard pages
  between them.
- **hardened_malloc** `[HARDENED-MALLOC]`: out-of-line metadata (§17.3 §3), size-class isolation,
  randomized allocation, guard pages, delayed/randomized free (quarantine) — a production security
  allocator.
- **XNU `kalloc_type` / Linux kmalloc hardening**: type-segregated kernel heaps, exactly to kill the
  "overflow object A into adjacent, more-useful object B" technique.
This is Lesson 10.5's "the allocator is a security-policy surface" taken to its conclusion: *choosing
the allocator and its layout is a mitigation.*

## 4. Defense in depth — the layers together
No single defence is sufficient; shipping allocators stack them so the attacker must defeat *all* of
them:
1. **ASLR + NX** (can't find or run code) — needs an info leak + ROP.
2. **Out-of-line / masked metadata + size checks** (§17.3) — can't corrupt bookkeeping blindly.
3. **Canaries + poison + double-free magic** (§17.2) — corruption that does happen is detected → abort.
4. **Guard pages + isolation** (§17.4) — linear overflow faults; cross-type corruption impossible.
5. **ASan/fuzzing in CI** (Lesson 16) — find the bugs before shipping.
Each layer converts some fraction of "bug → exploit" into "bug → crash" or "needs another bug." That
cumulative cost *is* practical memory safety on a language without it.

## Drills
1. Reproduce `guard.cpp`. Why does the guard page catch the overflow *earlier and more reliably* than a
   canary (§1 vs §17.2)? What does it cost that a canary doesn't?
2. Your capstone detector guard-pages each tracked allocation. Sketch the layout (allocation + guard)
   and explain what overflow it catches instantly and what it still misses (non-linear write).
3. Explain, using §3, why putting attacker-controlled strings in their own partition removes the
   "overflow into a vtable" technique *by construction*, not just detecting it.
4. Walk §4's layers against the §17.1 pipeline: for each exploitation stage, name the layer that blocks
   it and what the attacker needs to get past that layer.

## My summary
