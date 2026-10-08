# 18.3 — Writing a good harness

The fuzzer generates bytes; the **harness** decides what those bytes *do* to your code. The harness is
where most of the skill (and most of the missed bugs) live: a fuzzer can only find bugs its harness
can reach.

## 1. The fuzz target shape

The de-facto interface (libFuzzer's) is one function `[LIBFUZZER]`:

```cpp
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // interpret `data`/`size` and exercise the code under test; return 0
}
```

Your exercise uses the same shape (`fuzz_one(const uint8_t* data, size_t size)`). The rules:
- **Deterministic:** same input → same execution (no randomness, no clock, no uninitialised reads) —
  or the reproducer won't reproduce.
- **No persistent state across calls** (or reset it) — the fuzzer calls it millions of times in one
  process; leaked/lingering state makes crashes irreproducible.
- **Fast:** it runs millions of times; avoid I/O, sleeps, huge allocations.
- **Let it crash:** don't catch/suppress the error you're hunting — the sanitizer abort *is* the
  signal.

## 2. Mapping bytes to API calls

The art is turning a flat byte buffer into meaningful operations on your API:
- **A parser/codec** (Lesson 1 endian codec, a decoder): pass `data`/`size` straight in — the buffer
  *is* the input. Easiest and highest-value.
- **A container** (Lesson 11 `Vector`, Lesson 13 `SmallVector`): treat the bytes as a **program** — a
  sequence of opcodes: `data[i]` low bits pick an operation (push/pop/reserve/erase/index), the rest
  supply arguments. The fuzzer then explores operation *sequences*, finding the order that triggers an
  invalidation or overflow (Lesson 13.5).
- **An allocator** (Lesson 7 free-list, Lesson 17 hardened): bytes drive an alloc/free schedule —
  opcodes for `allocate(size)` / `deallocate(index)` with a table of live pointers — exploring the
  alloc/free orderings that cause double-free, overlap, or coalescing bugs.

The "bytes as a little bytecode" pattern is how you fuzz stateful APIs, and it's what makes fuzzing
find the *sequence-dependent* bugs manual tests miss.

## 3. Oracles beyond "it crashed"

A sanitizer catches memory errors, but you can assert *more* so the fuzzer finds logic bugs too:
- **Invariant oracle:** after each operation, check a structural invariant (your `Vector::size <=
  capacity`, the allocator's `check_invariants()` from Lesson 7, canaries from Lesson 17). A violated
  invariant → `abort()` → the fuzzer flags it.
- **Round-trip oracle:** `decode(encode(x)) == x` (Lesson 1 codec), `parse(serialize(x)) == x`. A
  mismatch is a bug even without a crash.
- **Differential oracle:** run the same input through your implementation **and** a reference
  (e.g., your `Vector` vs `std::vector`, your allocator vs `malloc`); any divergence is a bug. This is
  how you fuzz for *correctness*, not just safety.

Oracles turn the fuzzer from a crash-finder into a specification-checker.

## 4. Seeds and dictionaries
- **Seed corpus:** start the fuzzer from a few *valid* inputs (a real serialized record, a small valid
  file). Seeds pass the early checks instantly, so the fuzzer spends its budget on the deep code. A
  good seed corpus is often the difference between finding a bug in seconds vs never.
- **Dictionary:** a list of magic tokens/keywords the target checks for (`"FUZZ"`, format headers,
  SQL keywords). The mutator inserts them wholesale, cracking checks that per-byte mutation would take
  longer on. (Clang's `trace-cmp`, §18.2, does this automatically by feeding back comparison operands.)

## 5. Harnessing your own artifacts (the exercise + drills)
The exercise harnesses a small parser with a magic-gated overflow (the clean demonstration). The
drills point the same engine at your real artifacts:
- Lesson 1 codec — round-trip oracle;
- Lesson 7 allocator — alloc/free bytecode + `check_invariants()` oracle;
- Lesson 11/13 containers — operation-sequence bytecode + invariant/differential-vs-`std::vector`
  oracle.
Fuzzing your own code is the fastest way to discover that an exercise you "finished" still has an edge
case — which is exactly the researcher's loop.

## Drills
1. Write an operation-sequence harness for your Lesson-7 allocator: define the opcode encoding
   (alloc/free/index) and add `check_invariants()` as an oracle. What bug classes can it now find that
   a parser-style harness can't?
2. Add a round-trip oracle to a Lesson-1 codec harness. Construct the assertion and explain why a
   mismatch is a bug even though nothing crashed.
3. Why must the harness be deterministic and stateless across calls (§1)? Give a concrete way a
   lingering `static` would make a found crash fail to reproduce.
4. Build a differential harness: your `Vector<int>` vs `std::vector<int>`, same op sequence, compare
   contents each step. What's the first divergence you'd expect to catch, and is it a safety or a
   correctness bug?

## My summary
