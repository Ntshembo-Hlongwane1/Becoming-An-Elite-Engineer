# 17.2 — Canaries, poisoning, and double-free detection

The first line of allocator hardening: make the common bugs **detectable** at `free` time, cheaply, in
code you control — which matters because, inside a custom arena, the sanitizer can't (Lesson 10.5).

## 1. Heap canaries detect overflow

A **canary** is a known value placed where an overflow would hit before it reaches anything important.
You met the idea on the stack (Lesson 5.5: the compiler's stack-protector puts a random canary between
locals and the return address and checks it before `ret`). The heap version puts a canary **around
each allocation's payload** — just before it (header canary) and just after it (trailer canary) — and
checks both at `free`. A linear overflow past the payload overwrites the trailer canary; freeing the
block then notices "this isn't the value I wrote" → the overflow is caught.

Three properties make a canary hard to defeat:
- **Random**, so an attacker can't predict it.
- **Per-run secret-derived**, so leaking one program's canary doesn't help against another.
- **Per-block** (mixed with the block's address), so leaking one block's canary doesn't let the
  attacker forge another's. A *constant* canary is forgeable: an overflow that happens to write the
  constant value sails through. Your exercise's discriminating test checks exactly this — a constant-0
  canary fails; a secret-and-address-derived one passes.

### (measured) your allocator's canary catches what ASan cannot
In the exercise, the test overflows one byte past the payload into the trailer canary. Because the
whole pool is one `operator new` region, **AddressSanitizer sees nothing** (Lesson 10.5) — yet:
```
[ PASS ] canary_catches_heap_overflow      (overflow_detections() == 1)
```
`(measured)` The allocator's own canary check at `free` detected the intra-arena overflow ASan is blind
to. That's the entire argument for building hardening into a custom allocator.

## 2. Poison-on-free (clear secrets, surface UAF)

When a block is freed, overwrite its payload with a fixed poison pattern (e.g. `0xDD`). Two benefits:
- **Secrets don't linger.** A freed buffer that held a key/token is wiped, so a later info-leak can't
  read it (defense in depth).
- **Use-after-free is more likely to crash loudly** than to silently return stale-but-plausible data —
  a dereferenced poison pattern (e.g. `0xDDDDDDDDDDDDDDDD` as a pointer) faults instead of working.
It's cheap (`memset` on free) and your exercise does it. (ASan's version is "poison the shadow +
quarantine," Lesson 16.2; yours poisons the bytes directly.)

## 3. Double-free detection via a state magic

Give each block a **state** word — `ALLOCATED` vs `FREED` magic — set on allocate and on free. At
`free`, if the state is already `FREED`, it's a **double-free**; if it's neither magic, the pointer is
**invalid** (not one of ours, or corrupted). This is exactly glibc's **tcache key** (added 2.26): a
freed tcache chunk stores a per-thread key; freeing a chunk that already bears the key is caught as
"double free or corruption." Your exercise implements the same check and distinguishes double-free from
invalid-free. (ASan catches the same via quarantine + shadow, Lesson 16.2.)

## 4. What canaries do and don't do
- **Catch:** *linear* overflows that cross a canary, and (with the state magic) double/invalid frees —
  at `free` time.
- **Miss:** a **non-linear** write that jumps *over* the canary straight to a target (some format-string
  / arbitrary-index writes); a UAF *read* (nothing is overwritten); corruption read *before* the next
  `free`. Canaries are a tripwire checked at a specific moment, not continuous bounds checking.
- **Detection ≠ prevention:** a canary turns silent corruption into a *detected* corruption — then
  policy decides: abort (safest; most hardened allocators abort), or report. Your exercise reports via
  a `HeapViolation` exception + a counter so tests can assert detection without crashing; a shipping
  allocator would `abort()` to deny the attacker a second try.

Canaries + poison + the state magic are the "detect metadata corruption" layer. §17.3 adds "make the
free-list pointer itself unforgeable"; §17.4 adds "make the overflow fault in hardware."

## Drills
1. In the exercise, change `canary_for` to a constant and run `canary_is_secret_and_per_block` — why
   does it fail? Then make it `secret_ ^ mix(address)` and explain why per-block + secret beats a
   per-run-only canary against an attacker who can leak one block's canary.
2. Why is a canary checked at `free` unable to catch a UAF *read*? Which defence (§17.2 poison / §17.4
   guard page / Lesson 16 quarantine) catches that instead?
3. A shipping allocator `abort()`s on canary mismatch; your exercise throws. Give one security reason
   to prefer `abort()` (hint: what does continuing give the attacker?).
4. Relate the heap canary to the stack canary (Lesson 5.5): what's the same (random tripwire before the
   target) and what's different (per-block vs per-frame; checked at free vs at return)?

## My summary
