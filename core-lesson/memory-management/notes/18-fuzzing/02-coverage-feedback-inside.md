# 18.2 — How a coverage-guided fuzzer works inside

This is the file that makes you able to *build* one, not just run it. A coverage-guided fuzzer is four
parts: **instrumented coverage**, an **edge bitmap**, a **corpus**, and a **feedback loop** with
mutation. libFuzzer and AFL are elaborations of exactly this; you implement the core on GCC.

## 1. Instrumented coverage: the compiler tells you where execution went

Compile the **target** with coverage instrumentation and the compiler inserts a callback at the start
of every basic block (every branch target). GCC/Clang's `-fsanitize-coverage=trace-pc` `[SANCOV,
GCC-sancov]` inserts a call to:

```c
extern "C" void __sanitizer_cov_trace_pc(void);   // called at each instrumented edge
```

You define this function (in the fuzzer **engine**, which is compiled *without* coverage so it isn't
instrumented itself — otherwise it recurses). Inside it, the return address identifies the edge:

```cpp
extern "C" void __sanitizer_cov_trace_pc(void) {
    uintptr_t pc = (uintptr_t)__builtin_return_address(0);   // which edge called us
    g_covmap[(pc >> 4) & (MAP - 1)] = 1;                      // record it (see §2)
}
```

So after a run, you know *which edges of the target executed*. (Clang also offers `trace-cmp` to feed
back on comparison operands — how libFuzzer cracks `memcmp` magics without per-byte branches. GCC has
`trace-cmp` too; the exercise uses `trace-pc` and per-byte branches.)

## 2. The edge bitmap

Storing edges must be fast and allocation-free (the callback runs on *every branch of every exec*,
millions of times — Lesson 16.1's no-recursion/fixed-storage rule applies). AFL's design: a fixed
**bitmap**, one byte per edge-hash `[AFL]`:

```cpp
static constexpr unsigned MAP = 1u << 12;   // power of two
uint8_t g_covmap[MAP];                        // per-run: which edges fired (reset before each run)
```

The callback hashes the edge PC into `[0,MAP)` and sets (or increments) that byte. A power-of-two MAP
makes the index a cheap mask. Collisions (two edges → one slot) cost a little feedback precision;
AFL uses 64 KB, you can use less for small targets. **Key:** the callback must not allocate, lock, or
be coverage-instrumented — it's the hottest code in the system.

## 3. The corpus and the feedback loop

The **corpus** is the set of inputs the fuzzer has found *worth keeping* — the ones that reached new
coverage. The loop:

```
corpus = { seed }
total  = {}                         # every edge ever seen (cumulative bitmap)
repeat:
    input  = mutate(pick(corpus))   # pick a kept input, perturb it
    clear g_covmap                  # per-run coverage
    run target(input)               # fills g_covmap via the callback (may crash -> sanitizer aborts)
    if g_covmap has any edge not in total:   # "interesting": reached NEW code
        total |= g_covmap
        corpus.add(input)           # keep it; future mutations build on it
```

That `if new coverage → keep` line is the **entire idea** of coverage-guided fuzzing (§18.1 §2). It's
what lets the fuzzer crack a magic gate: an input whose first byte is correct reaches a branch the
others didn't → new edge → kept → mutated → its second byte gets cracked → and so on. Remove that line
(always "not interesting") and you have a dumb random fuzzer that never gets past the gate — the
measured contrast of §18.1 §4. **In the exercise, this function is exactly what you implement.**

## 4. Mutation

`mutate` perturbs a kept input to explore nearby inputs. A minimal but effective set `[LIBFUZZER,
AFL]`:
- **bit/byte flips** — `v[i] ^= 1<<k` or `v[i] = random_byte`;
- **grow/shrink** — append or erase bytes (to discover length-dependent paths);
- **splices / dictionary inserts** — combine two corpus entries, or insert known tokens (a
  "dictionary" of magic values) — how real fuzzers get past checksums/keywords faster.

The exercise implements a small mutator (flips + random bytes + grow); a drill adds a dictionary and
measures the speedup on the magic gate.

## 5. Catching the crash and saving the reproducer

The target runs **in-process** (fastest; libFuzzer does this too). When an input triggers a memory
bug, the **sanitizer aborts the whole process** — so you must record *which input* was running. Don't
write a file every iteration (that I/O dominates and slows the fuzzer ~100×); instead register a death
callback that dumps the current input only on crash:

```cpp
#include <sanitizer/common_interface_defs.h>
__sanitizer_set_death_callback(on_death);   // on_death() writes the current input to "crash-input"
```

That reproducer is the deliverable: a minimal input that deterministically re-triggers the bug under
the sanitizer (§18.4). (AFL instead forks a fresh process per input so a crash doesn't kill the
fuzzer; in-process is faster but a crash ends the run — fine, you've found the bug.)

## Drills
1. Why must the engine TU (defining `__sanitizer_cov_trace_pc`) be compiled **without**
   `-fsanitize-coverage`? Trace the infinite recursion that happens if you don't (you'll hit it if you
   try — it's a stack overflow in the callback).
2. The bitmap hashes edges into `MAP` slots. What does a collision cost (two edges → one slot), and
   why does AFL pick 64 KB? When is a smaller map fine (§2)?
3. Implement `interesting()` two ways: (a) always false, (b) real new-coverage check. Predict which
   finds the magic-gated bug in the budget, then confirm against §18.1 §4's measurement.
4. Why save the reproducer in a death callback rather than per iteration (§5)? Estimate the slowdown
   of writing a file on every exec at, say, 1M execs/s.

## My summary
