# 16.2 — How AddressSanitizer works inside

ASan is the tool you've leaned on all curriculum; here is its machinery. It is two parts `[ASAN-ALGO]`:
a **compile-time instrumentation** (added by `-fsanitize=address`) and a **runtime library** that
replaces the allocator. `[MEM §13.1]`: "AddressSanitizer is a runtime memory error detector … It is
built into modern versions of GCC and Clang," catching "Out-of-bounds accesses to heap, stack and
globals, Use-after-free, Use-after-return, Use-after-scope, Double-free, invalid free, Memory leaks"
`[ASAN-CLANG]`.

## 1. The allocator replacement: redzones + quarantine

ASan's runtime replaces `malloc`/`free` (§16.1 interception). Its `malloc` doesn't just hand back your
bytes — it wraps them `[ASAN-ALGO]`:

> "malloc allocates the requested amount of memory with **redzones** around it. The shadow values
> corresponding to the redzones are poisoned and the shadow values for the main memory region are
> cleared." `[ASAN-ALGO]`

A **redzone** is a poisoned guard region immediately before and after your allocation (conceptually
Lesson 7.2's header/footer, but marked "do not touch"). A read or write that steps one byte past your
buffer lands in a redzone → caught. That's heap **buffer-overflow** detection.

Its `free` doesn't immediately recycle the block `[ASAN-ALGO]`:

> "free poisons shadow values for the entire region and puts the chunk of memory into a **quarantine**
> queue (such that this chunk will not be returned again by malloc during some period of time)."
> `[ASAN-ALGO]`

Quarantine is the key to **use-after-free**: because the freed address is *not* reused for a while
(the opposite of Lesson 7.3's eager tcache reuse), a dangling pointer still points at a poisoned
region, so touching it is caught instead of silently hitting some new object. (Note this also defeats
the ABA-style reuse of Lesson 15 for the quarantine window.)

## 2. Shadow memory: checking every access in O(1)

Redzones and quarantine mark memory as poisoned — but *what checks the mark on every access*? **Shadow
memory**. ASan reserves a region where **one shadow byte describes eight application bytes**
`[ASAN-ALGO]`:

> mapping (64-bit): `Shadow = (Mem >> 3) + 0x7fff8000;` — an 8-to-1 correspondence.

The shadow byte encodes how much of that 8-byte "qword" is addressable `[ASAN-ALGO]`:
- **0** → "All 8 bytes … are unpoisoned (i.e. addressable)."
- **1–7** (`k`) → "First `k` bytes are unpoisoned, the rest `8-k` are poisoned." (a partial allocation
  ending mid-qword — this is why a `malloc(100)` whose size isn't a multiple of 8 still bounds-checks
  exactly: the last shadow byte is a `k`.)
- **negative** → "All 8 bytes … are poisoned (i.e. not addressable)." (redzones, freed/quarantined
  memory, globals' guards.)

The compiler instruments **every** load and store to check the shadow first `[ASAN-ALGO]`: compute the
shadow address `(Addr>>3)+offset`, read the shadow byte, and if the access isn't allowed, report and
abort — otherwise do the real access. In pseudocode it's a handful of instructions per access
(shift, add, load, compare, rare branch), which is why ASan is ~2× slowdown rather than Valgrind's
10–50×: the check is O(1) and mostly a predicted-not-taken branch.

### (measured) ASan in action, on your VM
```
use-after-free:  ERROR: AddressSanitizer: heap-use-after-free ... in main        (s_uaf.cpp)
double-free:     ERROR: AddressSanitizer: attempting double-free ... in main      (s_df.cpp)
leak:            ERROR: LeakSanitizer: detected memory leaks; 40 byte(s) leaked    (s_leak.cpp)
```
`(measured)` The UAF access hit a quarantined (poisoned) region; the double-free hit a chunk already
in quarantine; the leak is LeakSanitizer (§16.3). Each report carries the faulting access *and* the
allocation stack — because the runtime recorded metadata at `malloc` time (§16.1).

## 3. What this buys, and what it costs
- **Catches:** heap/stack/global overflow, use-after-free/return/scope, double/invalid free
  `[ASAN-CLANG]` — precisely, at the committing instruction, with stacks.
- **Costs:** ~2× CPU and a large memory overhead (shadow = 1/8 of the address space reserved, plus
  redzones and quarantine). A **debug/test** tool, not a production one (though hardened variants exist
  — Lesson 17).
- **Does not catch:** data races (that's **TSan**, Lesson 14 — and they can't be combined),
  uninitialised reads (that's **MSan**, `[MEM §13.1]`), or logic bugs like ABA (Lesson 15.3 §3).
  Knowing the tool boundaries is the point of this lesson.

## 4. Your exercise builds the leak half
Full shadow-memory instrumentation is a compiler feature — out of scope to hand-build. But the
**allocation-tracking + reporting** half (what LeakSanitizer does, §16.3) is exactly Lesson 8's hook
plus Lesson 7's metadata, and you'll build it (§16.4). The redzone/guard-page half — surrounding
allocations with poisoned/`PROT_NONE` guards to catch overflow — is Lesson 17's hardening, and the two
together are your capstone detector.

## Drills
1. `malloc(100)` under ASan: the 100 bytes span 12 full qwords + 4 bytes. What are the shadow bytes for
   the 13th qword and the redzone after it (§2 encoding)? Which shadow byte does `p[100]` check, and
   what does it hold?
2. Why does quarantine (not immediately reusing freed memory) enable UAF detection, and what's the cost
   vs Lesson 7.3's eager tcache reuse? When does a UAF escape ASan (hint: quarantine is finite)?
3. ASan is ~2× while Valgrind is 10–50×. Explain the difference from §2 (compile-time instrumentation +
   O(1) shadow check vs dynamic binary translation).
4. For each tool, name the bug it catches that the others don't: ASan, TSan, MSan (§3). Why can't ASan
   and TSan be combined in one build?

## My summary
