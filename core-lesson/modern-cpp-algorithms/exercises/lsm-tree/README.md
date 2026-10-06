# LSM-Tree Exercises

Two projects, deliberately **close to but not the same as** your object-storage project. They give
you every mechanism you will need there (logs, immutable files, headers/footers, checksums, manifest
+ atomic rename, crash recovery, page-sized blocks), without designing your object store for you.

| | Exercise 1 — `ex1-inmemory-lsm/` | Exercise 2 — `ex2-disk-lsm/` |
|---|---|---|
| Goal | the **data structure** and its **algorithms**, with real byte formats, all in RAM | the **storage engine**: files, pages, durability, crash recovery |
| Do it after | notes Part 5 | notes Part 6 (and Exercise 1) |
| Teaches (your list) | (a) the data structure, (b) how it works under the hood | (c) link to external storage, (d) page ordering, (e) metadata & headers |
| Memory topics | arena, alignment, placement new, atomics (release/acquire), RAII, views | aligned buffers, RAII fds, page cache vs `O_DIRECT`, syscalls |
| DSA topics | skip list, internal-key ordering, prefix-compressed blocks, Bloom filter, k-way merge, leveling | external-memory layout, block index, binary search over disk blocks, log framing |
| Produces | `libminilsm` (reused by Ex 2) | `libdisklsm` + `lsmdump` inspection tool |

## Rules of engagement

1. **Tests are the spec.** Each exercise ships interface headers whose function bodies `throw`
   `TODO`, and a test suite. Your job: replace every `Todo(...)` until `ctest` is green. You may change
   **private** members and add files freely; keep the **public** API the tests use.
2. **Milestones are ordered.** Each README lists milestones `M1…Mn` with the test filter that proves
   each one (`./tests <filter>`). Don't start Mk+1 until Mk is green **under sanitizers**.
3. **Sanitizers always on in Debug.** The CMake files enable `-fsanitize=address,undefined` in Debug
   (the workflow your algorithms book recommends `[ALGO §1.3.3, §3.3.3]`). Add a `tsan` build for the
   concurrency milestone.
4. **Every design decision gets one line in `DECISIONS.md`** with the source (lesson part / paper /
   man page) that justified it. This is the habit that makes you a systems engineer, and it's your
   documentation for the red-team phase later.
5. **No peeking at LevelDB until a milestone is green.** Then *do* read the corresponding LevelDB file
   and write down every difference in `DECISIONS.md`. ("Build before reading production
   implementations. Read production implementations afterwards." — your own repo README.)

## Build

```bash
cd ex1-inmemory-lsm
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/tests            # all tests
./build/tests coding     # only tests whose name contains "coding"
```
