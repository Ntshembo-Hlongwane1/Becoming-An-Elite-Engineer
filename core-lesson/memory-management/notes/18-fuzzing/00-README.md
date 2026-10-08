# Lesson 18 — Fuzzing Your Own Code (Phase F)

> Status: complete. Exercise: `../../exercises/18-fuzzing/`.
>
> Phase F is the research bridge: *find, reproduce, and understand* real memory bugs — on code **you**
> wrote. It opens with the single highest-yield bug-finding technique in the field: **coverage-guided
> fuzzing**. A fuzzer throws millions of mutated inputs at your code; a **sanitizer** (ASan/UBSan,
> Lesson 16) turns any memory error into an immediate, precise crash; and **coverage feedback** steers
> the mutations toward new code paths so the fuzzer cracks input checks that random testing never
> would. This pairing — fuzzer + sanitizer — is how a large fraction of real CVEs are found today
> (Lesson 16.5). You won't just *run* a fuzzer; you'll **build a minimal coverage-guided fuzzer
> yourself** (the AFL/libFuzzer mechanism: an edge bitmap from compiler-inserted coverage callbacks, a
> corpus, mutation, and feedback) and watch it find a bug that random fuzzing cannot — the "understand
> the mechanism, not just the tool" bar.

This is Phase F, Lesson 1 (applied research). It builds on Lesson 16 (sanitizers, interception) and the
artifacts of Lessons 1/7/11/13 (codecs, allocator, containers) as fuzz targets.

## Tooling note (your VM)
Production fuzzers (libFuzzer, AFL++) aren't installed here, and GCC doesn't ship libFuzzer. But GCC
**does** ship the same coverage instrumentation libFuzzer/AFL use (`-fsanitize-coverage=trace-pc`), so
you build the fuzzer engine yourself on the installed `g++` + ASan. The notes show how the same ideas
map onto libFuzzer/AFL++ (install `clang`/`afl++` to use those for real); the mechanism is identical.

## Ground rules recap
Every claim is quoted or marked **(measured)** (run on your VM — GCC 15.2.0). New sources at the
bottom and in `SOURCES.md`.

## Read in order
1. `01-why-fuzzing.md` — what fuzzing is; dumb vs mutational vs **coverage-guided**; the fuzzer+
   sanitizer oracle; why it beats manual review at scale. Measured: coverage-guided cracks a 4-byte
   magic gate + finds a heap overflow in <1 s; random does 20,000,000 tries and fails.
2. `02-coverage-feedback-inside.md` — how a coverage-guided fuzzer works *inside*: compiler-inserted
   edge callbacks (`-fsanitize-coverage=trace-pc`), the **edge bitmap**, the **corpus**, mutation, and
   the "keep inputs that reach new code" loop. This is what you build.
3. `03-harnesses.md` — the fuzz target (`LLVMFuzzerTestOneInput` shape), what makes a good harness,
   invariant/round-trip/differential oracles, seed corpora and dictionaries; harnessing your Lesson-7
   allocator (op sequences), Lesson-11/13 containers, Lesson-1 codec.
4. `04-triage-and-root-cause.md` — from a crash to a report: the reproducer, **minimization**,
   deterministic replay, root-causing under ASan, and the researcher writeup (discovery → root cause →
   primitive → fix).
5. `05-security-view.md` — fuzzing as the modern discovery loop (OSS-Fuzz, continuous fuzzing,
   fuzzer+sanitizer), its limits and blind spots, and defensive use (fuzz your own attack surface).
6. `06-glossary.md`.

Then do the exercise: **build a minimal coverage-guided fuzzer** — implement the coverage-feedback
decision and the mutator over a provided engine, point it at a target with a magic-gated heap
overflow, and watch it find and save a reproducer. The stub (coverage-blind) fails to find the bug in
the budget; your coverage-guided version finds it in under a second.

## The one-paragraph picture
A **fuzzer** generates many inputs and runs them through your code looking for crashes. A **dumb**
fuzzer uses random/noise inputs and gets stuck at the first input check (a magic number, a length
field). A **coverage-guided** fuzzer instead *learns*: the target is compiled with instrumentation that
records which **edges** (branches) each run executes into a small **bitmap**; the fuzzer keeps any
mutated input that reached a **new edge** in a **corpus**, then mutates the corpus further — so it
discovers input structure incrementally (one correct magic byte at a time) and drives execution deep
into the code. Pair it with a **sanitizer** (ASan, Lesson 16) and the instant an input causes a
memory error, you get a precise crash with the faulting and allocation stacks, plus the exact input
that triggered it (the **reproducer**). That fuzzer+sanitizer loop — run by Google's OSS-Fuzz on
thousands of projects continuously — is the dominant way memory bugs are found in 2026. You build the
coverage-feedback core yourself, on GCC's `-fsanitize-coverage=trace-pc`, and see it beat random by a
factor you can measure.

## New sources introduced here (also appended to `SOURCES.md`)
| Key | Source |
|---|---|
| `[LIBFUZZER]` | LLVM, *libFuzzer — a library for coverage-guided fuzz testing*. https://llvm.org/docs/LibFuzzer.html |
| `[SANCOV]` | LLVM, *SanitizerCoverage* — the `trace-pc`/`trace-cmp` instrumentation and callbacks GCC/Clang emit. https://clang.llvm.org/docs/SanitizerCoverage.html |
| `[AFL]` | M. Zalewski, *AFL / AFL++* — the edge-coverage bitmap and corpus-based mutational fuzzing. https://aflplus.plus/ |
| `[OSSFUZZ]` | Google, *OSS-Fuzz* — continuous fuzzing of open-source software with libFuzzer/AFL + sanitizers. https://google.github.io/oss-fuzz/ |
| `[GCC-sancov]` | GCC manual, `-fsanitize-coverage=trace-pc` / `trace-cmp`. https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html |
