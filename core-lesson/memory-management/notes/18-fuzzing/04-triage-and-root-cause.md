# 18.4 — From a crash to a report: triage and root cause

A fuzzer gives you a crashing input and a sanitizer stack. That's the *start* of the work, not the
end. A researcher turns it into a minimal reproducer, a root cause, an assessed primitive, and a fix.
This is the writeup discipline that separates "I ran a fuzzer" from "I found and understood a bug."

## 1. Reproduce deterministically

First, confirm the crash is real and repeatable: run the target on the saved reproducer directly (the
exercise's `replay`), outside the fuzzer, under the sanitizer. If it crashes the same way every time,
you have a deterministic bug. If it's flaky, your harness has nondeterminism (§18.3 §1) — fix that
first, or you can't analyze it. Keep the reproducer file; it *is* the bug report.

## 2. Minimize

A raw reproducer is often bloated (the mutator added junk). **Minimization** shrinks it to the
smallest input that still triggers the same crash — which usually *is* the root-cause insight, because
what's left is exactly the bytes that matter. The algorithm (what libFuzzer's `-minimize_crash` and
`afl-tmin` do): repeatedly try removing/zeroing chunks; keep any smaller input that still crashes the
same way. Your `"FUZZ"`+len reproducer minimizes to 6 bytes — `46 55 5a 5a` + one length byte > 16 —
and now the bug is obvious from the input alone.

## 3. Root-cause under the sanitizer

Read the ASan report (Lesson 16.2) — it names the bug class, the object, and two stacks:
- the **faulting access** stack (where the bad read/write happened), and
- the **allocation** stack (where the object was created; and for UAF, the **free** stack).

Walk from the faulting line back to *why*: a length not validated against the buffer size, an index
derived from input, a missing bounds check, an off-by-one. For the exercise's bug the report points at
`buf[i] = i` with `i` up to `len` (input byte) but `buf` only 16 — an unchecked length from untrusted
input (the Lesson-1 / Lesson-7.5 pattern). gdb on the reproducer (Lesson 20 previews this) lets you
inspect the exact state at the fault.

## 4. Assess the primitive (the researcher's judgment)

Don't stop at "heap-buffer-overflow." State **what it gives an attacker and what it still needs** —
the honest-ceiling discipline from every security view:
- *What:* a heap write past a 16-byte buffer, length and (partially) contents attacker-controlled →
  the Lesson-7.5/17.1 metadata-corruption primitive.
- *Reachability:* behind a 4-byte magic — trivially reachable by an attacker who controls the input.
- *Ceiling:* with heap grooming → control an adjacent chunk/pointer → potential write-what-where; still
  gated by ASLR/allocator hardening + likely an info leak (Lesson 17.1 §5).
This paragraph is what makes a bug report *useful* to a maintainer or a weaponization decision.

## 5. The writeup (the deliverable that signals "researcher")

A real finding is written up, every time, in a fixed shape:
1. **Summary** — one line: bug class, component, impact.
2. **Reproducer** — the minimized input + exact build/run commands (sanitizer flags).
3. **Root cause** — the specific code and why (with the source line / stacks).
4. **Impact / primitive** — §4's honest assessment.
5. **Fix** — the patch (here: validate `len <= 16`), and ideally the *class* fix (bounds-carrying
   type, Lesson 1/13; or the Lesson-17 hardening that would have caught it).
6. **Regression** — add the reproducer as a seed/test so it never comes back.

The exercise produces #1–#5 for its seeded bug; the drills do the full loop on a bug you find in your
own earlier code. That artifact — reproducer + root cause + impact + fix — is portfolio gold and the
exact output a security team or a CVE report expects.

## Drills
1. Take the exercise reproducer and minimize it by hand (zero/remove bytes, re-run `replay`, keep if
   it still crashes). What's the smallest input that still triggers it, and what does that reveal about
   the root cause?
2. Read the ASan report's two stacks (faulting + allocation). In one sentence each, say what each tells
   you and how together they pinpoint the bug.
3. Write the §5 writeup for the exercise bug end to end, including the honest-ceiling impact paragraph
   (§4) and both the point fix and the class fix.
4. Fuzz one of your earlier exercises (Lesson 7/11/13) with an invariant oracle (§18.3). If it finds a
   crash, do the full triage→writeup loop; if it doesn't after a real budget, what does that (weakly)
   tell you, and what doesn't it tell you?

## My summary
