# Exercise 1 — Integers & Representation

Implement the stubs until `./run.sh` prints `ALL TESTS PASSED`. Notes:
`../../notes/01-integers-and-representation/`.

| Part | File you edit | Tests | Lesson |
|---|---|---|---|
| 1a checked arithmetic | `src/checked.cpp` | `./run.sh checked` | §1.3–§1.4 |
| 1b fixed-width codec | `src/codec.cpp` | `./run.sh codec` | §1.5 |
| 1c safe record parser | `src/record.cpp` | `./run.sh record` | §1.5–§1.6 |

Rules:
1. Debug build has **ASan + UBSan on**. A run that prints `ALL TESTS PASSED` but then shows a
   sanitizer report has FAILED — read the whole output. The codec's high-bit test and the record's
   huge-length test are there specifically to trip UB.
2. The contracts are in the header comments; the tests are the spec.
3. One sourced line per section in `DECISIONS.md`.
4. After green, read LevelDB `util/coding.{h,cc}` (the real `EncodeFixed*`/`GetVarint*`) and note
   the differences.

Run: `cmake`/`ctest` are wrapped by `./run.sh`; you can also `./run.sh <filter>`.
