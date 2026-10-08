# 6.5 — The Security View: Caches Leak Timing

Caches make programs fast by making some accesses faster than others — and that *difference* is
observable, which turns the cache into a side channel. This section is conceptual and defensive: the
goal is to recognise and avoid timing leaks in your own code, not to build attacks.

## The idea
Whether a given line is in cache changes how long an access takes (§6.1: L1 ~1 ns vs DRAM ~100 ns).
If *what you access* depends on a secret, an observer who can time memory accesses can infer the
secret. Two shapes:
- **Secret-dependent memory access:** e.g. a table lookup `T[key[i]]` (classic in naive AES) loads a
  line chosen by a secret byte; an attacker who can tell which line is cached learns the byte.
- **Secret-dependent branch:** a branch taken on a secret loads different code/data, with different
  cache/timing footprints.

Microarchitectural attacks (Flush+Reload, Prime+Probe, and the speculative-execution family —
Spectre/Meltdown) exploit exactly these timing differences; `[HPC §4.5]` references ASLR/side-channel
bypass techniques at a high level. The mechanism you need to internalise is just "cache hit vs miss
is measurable, so access patterns leak."

## Offense — discovery (at the level this course cares about)
- Look for **secret-dependent indexing or branching** in security-sensitive code: crypto key
  schedules, password/MAC comparison that returns early, table lookups keyed by secret bytes.
- The tell is: "does the address I touch, or the branch I take, depend on data that must stay secret?"

## Offense — value, honest ceiling
- **Ceiling:** recovery of secret keys/data across isolation boundaries (process, VM) *given* the
  attacker can co-locate and time precisely — powerful but situational. Speculative variants can read
  memory the code never architecturally accesses.
- **Floor / reality:** many timing differences are not practically exploitable; demonstrating a real
  leak requires careful measurement and co-location. Distinguishing "theoretically leaks" from
  "practically exploitable" is, again, the researcher's judgment.

## Defense — construction
- **Constant-time code** for secrets: no secret-dependent branches or memory indices. Compare secrets
  with a constant-time equality (`CRYPTO_memcmp`-style: fold all bytes, no early return), not
  `memcmp` / `==` that returns at the first difference.
- **Don't index tables by secret bytes**; use bitsliced or hardware (AES-NI) implementations for crypto.
- **Flush/partition** sensitive data where the platform offers it; keep secrets out of shared caches.
- This is adjacent to your core work rather than central to it, but as you move toward security
  research it's the reason "it computes the right answer" is not the same as "it's secure."

## Through-line
Lesson 6's performance lens (hit vs miss) and security lens (hit vs miss is *observable*) are the same
fact seen twice. Your capstone doesn't defend against timing attacks, but the measurement discipline
here — timing memory precisely, attributing it to cache behaviour — is the same skill a side-channel
researcher uses, and the same one your write-up needs.

## My summary
