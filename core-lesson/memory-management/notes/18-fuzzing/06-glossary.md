# Lesson 18 — Glossary

| Term | One line | § |
|---|---|---|
| fuzzing | run many generated inputs through code, watch for crashes | 18.1 |
| dumb / random fuzzer | random bytes; stuck at the first input check | 18.1 |
| mutational fuzzer | mutate valid seed inputs; no feedback | 18.1 |
| coverage-guided fuzzer | keep inputs that reach new code edges; learns structure | 18.1/18.2 |
| oracle | how the fuzzer decides an input is "bad" | 18.1/18.3 |
| fuzzer + sanitizer | coverage-guided fuzzer driving an ASan/UBSan target (the loop) | 18.1 |
| magic gate | an input check (magic bytes/length) random fuzzing can't pass | 18.1 |
| SanitizerCoverage | compiler instrumentation emitting per-edge callbacks | 18.2 |
| `-fsanitize-coverage=trace-pc` | GCC/Clang flag; calls `__sanitizer_cov_trace_pc` per edge | 18.2 |
| edge / basic block | a straight-line code segment; coverage counts entering it | 18.2 |
| edge bitmap | fixed byte array recording which edges a run hit (AFL-style) | 18.2 |
| corpus | the inputs the fuzzer kept (each reached new coverage) | 18.2 |
| feedback loop | keep input iff it hit a new edge; the core of coverage-guided | 18.2 |
| mutation | perturb a kept input (flip/replace/grow/splice) | 18.2 |
| trace-cmp | feed back comparison operands (crack memcmp magics) | 18.2 |
| in-process fuzzing | run the target in the fuzzer's process (fast; crash ends run) | 18.2 |
| death callback | `__sanitizer_set_death_callback`; dump reproducer on crash | 18.2 |
| harness / fuzz target | maps input bytes to API calls (`LLVMFuzzerTestOneInput`) | 18.3 |
| bytes-as-bytecode | encode an operation sequence in the input (stateful APIs) | 18.3 |
| invariant / round-trip / differential oracle | check structure / decode∘encode / vs a reference | 18.3 |
| seed corpus | valid starting inputs that pass early checks | 18.3 |
| dictionary | known tokens the mutator inserts to pass checks faster | 18.3 |
| reproducer | the minimal input that re-triggers the crash | 18.4 |
| minimization | shrink a reproducer to the bytes that matter | 18.4 |
| triage / root cause | reproduce → minimize → read stacks → find the cause | 18.4 |
| OSS-Fuzz | Google's continuous fuzzing of OSS with libFuzzer/AFL + sanitizers | 18.5 |
