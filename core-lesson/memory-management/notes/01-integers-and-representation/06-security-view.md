# 1.6 — The Security Researcher's View: Integers as the Root Cause

`[MEM §5.1]` shows the *effect* — a buffer overflow:

```cpp
char buffer[10];
strcpy(buffer, "This string is too long");   // writes past the array
```

But `strcpy` here fails because a **length** (the source length, 23) exceeded a **capacity** (10).
Almost every memory-corruption bug has an integer at its root: a size computed wrong, a length that
wrapped, an index that went negative, a signed/unsigned comparison that flipped. Harden the
integers (Lessons 1.3–1.4) and the overflow in Lesson 17 never gets its length. Three hats, same as
the LSM/alignment lessons.

## Bug class: the integer-overflow → undersized-allocation → heap overflow chain

The pattern, in one derivation:

```cpp
void* buf = malloc(count * size);      // (1) count, size attacker-influenced
for (i = 0; i < count; ++i)
    memcpy((char*)buf + i*size, src[i], size);   // (2) writes count*size bytes
```

If `count * size` overflows (§1.3, §1.4's promotion), step (1) allocates a **small** buffer — the
wrapped product — while step (2) writes the **full, large** number of bytes. The gap is a heap
overflow with attacker-controlled contents. The multiply is the root; the overflow is the effect.

### Offense — discovery
- **Review heuristic:** grep every allocation size that contains `*` or `+` or `<<` and ask "can
  the operands come from input, and can the math wrap?" `malloc(a*b)`, `new T[n]`, `resize(len)`,
  `reserve(count)`. Also every `size() - k`, every `len - header`, every `abs()`/negate of a parsed
  int (§1.3 asymmetry).
- **Black-box:** feed boundary values wherever a length/count enters — `0`, `1`, values near
  `2^16`, `2^31`, `2^32`, `2^64`, and negatives where a signed field is read. A size field set to
  `0xFFFFFFFF` that makes a service allocate `0` and then write a lot is the tell.
- **Fuzzing + UBSan/ASan:** build the target with `-fsanitize=undefined,address` and fuzz. Signed
  overflow, out-of-range shift, and the resulting OOB write all get caught at the moment they
  happen (§1.3 measured these).

### Offense — value (honest ceiling)
- **Ceiling: heap/stack memory corruption → potentially code execution**, because the write is often
  attacker-sized and attacker-content. That is the high end.
- **Floor:** many integer bugs only yield a wrong length that causes an over-read
  (**information disclosure**, e.g. Heartbleed-style "give me N bytes" with N too big) or a crash
  (**DoS**). Distinguishing "this wraps but the result is only used as a loop bound that then
  bounds-checks" from "this wraps into an allocation size" is the core skill — not every overflow is
  exploitable, and saying which is which precisely is what separates a researcher from a scanner.
- A famous instance of the multiply form is **CVE-2002-0639** (OpenSSH): a count read from the
  protocol was multiplied to size an allocation and overflowed, corrupting the heap
  `[CVE-2002-0639]`. (Mechanism only — this course reproduces the *bug class* in your own code,
  per your project's methodology, not a weaponised exploit.)

### Defense — impact and "impossible by construction"
- **Business impact:** a remotely reachable size-overflow in a service is, at the ceiling, full node
  compromise (confidentiality + integrity + availability); at the floor, a crash loop (availability)
  or a data leak (confidentiality/compliance). An attacker-controlled length is never a "low"
  finding until you've shown the ceiling is only DoS.
- **By construction:**
  1. **Checked arithmetic on every size** (§1.3): pre-check (`b > MAX - a`, `b > MAX/a`) or
     `__builtin_mul_overflow`. Refuse, don't wrap.
  2. **Right types** (§1.4): `size_t` for sizes, no signed/unsigned mixing; `std::cmp_*` for
     unavoidable mixed comparisons; brace-init to forbid narrowing.
  3. **Warnings as errors:** `-Wconversion -Wsign-conversion -Wsign-compare`.
  4. **Sanitizers in test/CI** and `-fwrapv` or `-ftrapv` where you want overflow defined or trapping
     rather than exploitable `[GCC-fwrapv]`.
  5. **Bound inputs early:** reject an implausible length *before* any arithmetic, so the math can't
     reach the dangerous range at all. CERT INT30-C/INT32-C codify this `[CERT-INT30]`.

## The through-line for the rest of the course
- Lesson 2: the `ByteReader` must compute `pos + n` without wrapping and bounds-check before every
  read — this bug class, defended at the parser.
- Lesson 7 / Lesson 9: allocator size math with the overflow guard (you already saw CVE-2013-4332 in
  the alignment lesson — an align-up overflow, the same family).
- Lesson 17: the heap/stack overflow whose *root* you now know how to remove.

## My summary
