# Exercise 17 — Harden a free-list allocator

Implement the hardening primitives in `include/mm/hardened_allocator.hpp` until `./run.sh` prints
`ALL TESTS PASSED`. Notes: `../../notes/17-attack-and-hardening/`.

This closes the loop: you built a free-list allocator (Lesson 7), saw its metadata turned into
arbitrary writes (Lesson 7.5), and now you add the defences glibc / PartitionAlloc / hardened_malloc
ship. The allocator carves one backing region into fixed blocks — so **AddressSanitizer cannot see an
overflow from one block into the next** (Lesson 10.5); the allocator must detect it itself.

## What you implement (the security primitives)
- **`canary_for(addr)`** — a **secret, per-block** canary (`secret_ ^ mix(addr)`). A constant is
  forgeable; mixing the secret and the block address makes it unguessable and non-reusable.
- **`encode_link`/`decode_link`** — **safe-linking** (glibc 2.32): store the free-list `next` XORed
  with `(slot >> 12)`, so a leaked or overwritten link is useless without knowing where it lives.
  Must round-trip: `decode(encode(p, slot), slot) == p`.
- (`arm_canaries`/`canaries_intact` are provided and call your `canary_for`.)

The allocator plumbing is provided: `allocate`/`deallocate`, the double-free / invalid-free state
magic, poison-on-free, the decoded-link in-range check, the detection policy (throw `HeapViolation`
and bump a counter), and the random `secret_`. Read them — they call your primitives.

## The stub, and your job
The stub primitives are **functional but INSECURE**: `canary_for` returns a constant `0`, and
`encode_link`/`decode_link` are the identity (no obfuscation). The allocator still *works*, and basic
overflow/double-free/invalid-free detection still passes — but two **discriminating** tests fail:
- `canary_is_secret_and_per_block` — a constant canary is `0` and the same for every block (forgeable);
- `safe_linking_obfuscates_the_pointer` — an identity "encoding" doesn't obfuscate the pointer.

Make the canary secret-and-address-derived and the link actually XOR-obfuscated, and all pass.

## What the tests check (Lesson 17 defences)
- allocate/deallocate round-trip; pool exhaustion → `nullptr`;
- the canary is secret and per-block; safe-linking obfuscates yet round-trips;
- a **heap overflow** past the payload is caught by the canary (the one ASan can't see);
- **double-free** and **invalid-free** (foreign pointer) are caught;
- a **corrupted free-list link** is rejected on the next allocate (safe-linking + in-range check).

All under AddressSanitizer + UBSan — which pass *because* the point is that ASan is blind to the
intra-arena corruption and your hardening catches it.

Run: `./run.sh` or `./run.sh <filter>`. Then fill in `DECISIONS.md`.
