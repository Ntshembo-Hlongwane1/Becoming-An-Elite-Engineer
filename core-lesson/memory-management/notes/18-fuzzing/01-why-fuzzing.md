# 18.1 — Why fuzzing

## 1. The problem with testing memory code by hand

Unit tests (every exercise so far) check the inputs *you thought of*. Memory bugs hide in the inputs
you didn't: the length that's one too big, the byte sequence that hits an unhandled branch, the
alloc/free order that aliases a chunk. You cannot enumerate those by hand at scale — there are too
many. **Fuzzing** automates the search: generate huge numbers of inputs, run them through the code,
and watch for crashes. Pair it with a sanitizer (Lesson 16) and "crash" means "precisely located
memory bug with stacks," not "maybe something's wrong."

## 2. Three kinds of fuzzer

- **Dumb / random** — feed random bytes. Finds shallow bugs instantly, but gets stuck at the first
  *input check*: a 4-byte magic number, a checksum, a length field. Random bytes satisfy a 4-byte
  magic with probability 1/2³² — effectively never. Most interesting code is behind such checks.
- **Mutational** — start from valid **seed** inputs and mutate them (flip bits, splice, change
  lengths). Better, because seeds already pass the early checks — but without feedback it still can't
  *discover* new structure on its own.
- **Coverage-guided** — the breakthrough (AFL, libFuzzer). The target is instrumented to report which
  **code edges** each run executes; the fuzzer **keeps any mutated input that reached a new edge** and
  mutates those further. It thus *learns* input structure incrementally and drives execution deep into
  the code — cracking magic numbers one byte at a time, because each correct byte opens a new branch
  that registers as new coverage. This is the technique that matters, and the one you build (§18.2).

## 3. The fuzzer + sanitizer oracle

A fuzzer needs an **oracle**: how does it know an input is "bad"? A plain crash (SIGSEGV) catches only
the memory bugs that happen to fault. A **sanitizer** is a far better oracle (Lesson 16): compiled
with ASan/UBSan, the target turns *any* heap overflow, use-after-free, double-free, or UB into an
immediate, precise abort — even the silent ones that would otherwise corrupt and continue. So the
standard loop is **coverage-guided fuzzer driving a sanitizer-instrumented target**. That combination
— run continuously by Google's **OSS-Fuzz** across thousands of projects — is how a large share of
real CVEs are found today (Lesson 16.5).

## 4. (measured) coverage-guided vs random, on your VM

A target with a 4-byte magic gate (`"FUZZ"`) protecting a heap-buffer-overflow, fuzzed two ways:

```
coverage-guided:  cracked FUZZ + found the overflow in < 1 s   (reproducer: 46 55 5a 5a d4 ...  = "FUZZ"+len 0xd4)
coverage-blind (random):  20,000,000 iterations, NO crash      (can't guess the 4 magic bytes: ~1 in 2^32)
```
`(measured; the exercise)` Same target, same mutator, same budget — the *only* difference is whether
the fuzzer keeps inputs that reached new coverage. Feedback turns an intractable 2³² guess into a
sub-second find, because each correct magic byte opens a new branch the fuzzer rewards and builds on.
That factor *is* the reason coverage-guided fuzzing replaced random fuzzing everywhere.

## 5. What fuzzing is good and bad at
- **Great at:** memory-safety bugs on code that parses/processes untrusted input (parsers, codecs,
  decoders, allocators, containers) — exactly the Lesson-7.5/13.5 bug classes. Finds inputs you'd
  never think of; gives a reproducer.
- **Weak at:** logic bugs with no crash oracle (wrong result that isn't UB — unless you add an
  invariant/differential oracle, §18.3); deep stateful protocols without a structure-aware mutator;
  bugs needing very specific environment/timing (some races — though there are concurrency fuzzers).
- **Needs:** a **harness** (how bytes map to API calls, §18.3) and a **sanitizer** oracle. The harness
  quality determines what it can reach.

Next: how coverage feedback actually works inside — the thing you implement.

## Drills
1. Compute the expected number of random tries to satisfy a 4-byte magic (§2). Now explain why
   coverage-guided needs only ~4×256 lucky *byte* mutations instead (§2/§4) — what does each correct
   byte "unlock"?
2. Why is a sanitizer a better oracle than a bare SIGSEGV handler (§3)? Name a bug class that silently
   corrupts without faulting that ASan still catches (Lesson 16.2).
3. Give two targets from earlier lessons that are ideal fuzz candidates and one that isn't, and say
   why (§5). What oracle would you add to fuzz a pure-logic function with no crash?
4. The measured random run did 20M execs and found nothing; coverage-guided found it in <1 s. If you
   only had a random fuzzer, what *one* change to the target (not the fuzzer) would make it findable,
   and why is that cheating in real life?

## My summary
