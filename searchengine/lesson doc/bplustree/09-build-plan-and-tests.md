# 09 — Build Plan & Test Suite

> **The doc you actually work from.** Nine checkpoints, each independently verifiable, each
> building on the last. Do them in order. Do not start checkpoint N+1 until N's tests are
> green — B+Tree bugs compound viciously, and a split bug found while debugging delete costs
> ten times what it costs found in isolation.
>
> **Time estimate:** CP1–CP4 in a focused day. CP5 (delete) is genuinely another day on its
> own. CP6–CP9 as you need them.

---

## 1. The nine checkpoints

| CP | Deliverable | Done when | Doc |
|---|---|---|---|
| **1** | Constants, `Node`, `printTree`, **validator** | Hand-built trees validate; each of I1–I8 fails when you deliberately break it | 02 |
| **2** | `lowerBoundIdx`, `upperBoundIdx`, `find`, `scan`, `scanPrefix`, iterator | All queries correct on a hand-built 3-leaf tree, **including a key equal to a separator** | 03 |
| **3** | `splitLeaf`, `splitInternal`, `growNewRoot` | Unit-tested in isolation against §4's before/after states | 04 §3–5 |
| **4** | `insert` | 1000 ascending + 1000 descending + 1000 shuffled, `validate()` after **every** insert | 04 §6 |
| **5a** | `removeLazy` | Deletes work; I1–I3, I5–I8 hold (I4 relaxed) | 05 §1 |
| **5b** | 6 repair fns, `repairChild`, `remove`, `collapseRootIfNeeded` | Each repair unit-tested alone; then the `std::map` oracle passes 100k mixed ops | 05 §4–7 |
| **6** | Rule of five, `clear`, templated `Compare`, sanitizer-clean | ASan/UBSan report nothing; copy/move tests pass | 06 |
| **7** | `bulkLoad`, benchmark harness, fanout sweep | Numbers recorded in a comment at the top of the header | 07 |
| **8** | `shortestSeparator`, flat node layout *(optional)* | Measured improvement, or a documented decision not to keep it | 07 §3, §7 |
| **9** | Page format, `bulkLoadToFile`, mmap reader *(optional)* | Disk tree's answers match the in-memory tree exactly | 08 §9 |

**The critical path is CP1 → CP4.** After CP4 you have a working, verified, self-balancing
B+Tree, which is the thing you set out to build. CP5b is the hard part; CP6–9 are polish and
extension.

---

## 2. `printTree` — build this at CP1, use it 200 times

```cpp
void printTree(std::ostream& os) const {
    if (!root_) { os << "<empty>\n"; return; }
    std::vector<std::pair<Node*, int>> stack{{root_, 0}};
    while (!stack.empty()) {
        auto [n, depth] = stack.back();
        stack.pop_back();

        os << std::string(depth * 4, ' ') << (n->isLeaf ? "LEAF " : "NODE ") << "[";
        for (std::size_t i = 0; i < n->keys.size(); ++i)
            os << (i ? " " : "") << n->keys[i];
        os << "]";
        if (n->isLeaf) os << "  next=" << (n->next ? "→" : "∅");
        os << "\n";

        if (!n->isLeaf)
            for (auto it = n->children.rbegin(); it != n->children.rend(); ++it)
                stack.emplace_back(*it, depth + 1);   // reverse → prints left-to-right
    }
}
```

Output for doc 04 §7's final tree:

```
NODE [70]
    NODE [30 50]
        LEAF [10 20]  next=→
        LEAF [30 40]  next=→
        LEAF [50 60]  next=→
    NODE [90]
        LEAF [70 80]  next=→
        LEAF [90 100 110]  next=∅
```

You can verify every invariant by eye from this. Print it whenever a test fails, before you
open a debugger — nine times out of ten the bug is visible in the shape.

---

## 3. The test suite — your codebase's style

Matching `tests/bstree_test.cpp`: one `void test_x()` per case, `assert`, `[PASSED]` print,
called from `main`.

```cpp
// internal/kernal/core/datastructures/tests/bplustree_test.cpp
#include <cassert>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <vector>
#include "../bplustree.hpp"

// Small fanout → every test forces real splits and merges, and every tree is
// small enough to print and check by hand.
using Tree = BPlusTree<int, std::string, std::less<int>, 4, 4>;

// -------------------------------------------------------------------- CP1/CP2
void test_empty_tree() {
    std::cout << "Running test_empty_tree..." << std::endl;
    Tree t;
    assert(t.empty());
    assert(t.size() == 0);
    assert(t.find(42) == nullptr);
    assert(!t.erase(42));
    t.validate();
    std::cout << "  [PASSED] Empty Tree" << std::endl;
}

void test_single_insert_find() {
    std::cout << "Running test_single_insert_find..." << std::endl;
    Tree t;
    assert(t.insert(10, "ten"));            // true == new key
    assert(t.size() == 1);
    assert(t.find(10) && *t.find(10) == "ten");
    assert(t.find(11) == nullptr);
    assert(!t.insert(10, "TEN"));           // false == overwrote
    assert(t.size() == 1);                  // ← count must NOT increment on overwrite
    assert(*t.find(10) == "TEN");
    t.validate();
    std::cout << "  [PASSED] Single Insert/Find" << std::endl;
}

// -------------------------------------------------------------------- CP4
void test_leaf_split() {
    std::cout << "Running test_leaf_split..." << std::endl;
    Tree t;                                  // LEAF_MAX = 4
    for (int k : {10, 20, 30, 40}) t.insert(k, "v");
    assert(t.height() == 1);                 // still a single leaf
    t.insert(50, "v");                       // → split → root split
    assert(t.height() == 2);
    for (int k : {10, 20, 30, 40, 50}) assert(t.find(k) != nullptr);
    t.validate();
    std::cout << "  [PASSED] Leaf Split" << std::endl;
}

void test_root_split_to_height_3() {
    std::cout << "Running test_root_split_to_height_3..." << std::endl;
    Tree t;
    for (int k = 10; k <= 110; k += 10) { t.insert(k, "v"); t.validate(); }
    assert(t.height() == 3);                 // exactly doc 04 §7's trace
    assert(t.size() == 11);
    for (int k = 10; k <= 110; k += 10) assert(t.find(k) != nullptr);
    std::cout << "  [PASSED] Root Split → Height 3" << std::endl;
}

void test_insert_orders() {
    std::cout << "Running test_insert_orders..." << std::endl;
    const int N = 1000;
    std::vector<int> asc(N), desc(N), shuf(N);
    for (int i = 0; i < N; ++i) { asc[i] = i; desc[i] = N - i; shuf[i] = i; }
    std::shuffle(shuf.begin(), shuf.end(), std::mt19937(42));

    for (const auto& order : {asc, desc, shuf}) {
        Tree t;
        for (int k : order) { t.insert(k, "v"); t.validate(); }   // validate EVERY insert
        assert(t.size() == (std::size_t)N);
        for (int k : order) assert(t.find(k) != nullptr);

        std::vector<int> seen;                                     // full scan must be sorted
        for (auto it = t.begin(); it != t.end(); ++it) seen.push_back((*it).first);
        assert(seen.size() == (std::size_t)N);
        assert(std::is_sorted(seen.begin(), seen.end()));
    }
    std::cout << "  [PASSED] Insert Orders (asc/desc/shuffled)" << std::endl;
}

// -------------------------------------------------------------------- CP2
void test_separator_equal_key() {
    std::cout << "Running test_separator_equal_key..." << std::endl;
    // THE upper_bound test. Every key that becomes a separator must stay findable.
    Tree t;
    for (int k = 1; k <= 200; ++k) t.insert(k, "v");
    for (int k = 1; k <= 200; ++k)
        assert(t.find(k) != nullptr && "a key equal to a separator went missing — "
                                       "internal routing is using lower_bound (doc 03 §2)");
    std::cout << "  [PASSED] Separator-Equal Keys Findable" << std::endl;
}

void test_range_scan() {
    std::cout << "Running test_range_scan..." << std::endl;
    Tree t;
    for (int k = 0; k < 100; ++k) t.insert(k, "v");

    std::vector<int> got;
    t.scan(25, 75, [&](const int& k, const std::string&) { got.push_back(k); });
    assert(got.size() == 50);                       // [25, 75) half-open
    assert(got.front() == 25 && got.back() == 74);
    assert(std::is_sorted(got.begin(), got.end()));

    got.clear();                                    // empty range
    t.scan(50, 50, [&](const int& k, const std::string&) { got.push_back(k); });
    assert(got.empty());

    got.clear();                                    // range entirely past the end
    t.scan(1000, 2000, [&](const int& k, const std::string&) { got.push_back(k); });
    assert(got.empty());
    std::cout << "  [PASSED] Range Scan" << std::endl;
}

// -------------------------------------------------------------------- CP5
void test_delete_to_empty() {
    std::cout << "Running test_delete_to_empty..." << std::endl;
    const int N = 1000;
    std::vector<int> keys(N);
    for (int i = 0; i < N; ++i) keys[i] = i;

    Tree t;
    for (int k : keys) t.insert(k, "v");
    std::shuffle(keys.begin(), keys.end(), std::mt19937(7));   // delete in RANDOM order

    for (std::size_t i = 0; i < keys.size(); ++i) {
        assert(t.erase(keys[i]));
        assert(!t.erase(keys[i]));                              // idempotent: second is a no-op
        t.validate();                                           // after EVERY delete
        assert(t.size() == (std::size_t)(N - i - 1));
    }
    assert(t.empty());
    assert(t.height() == 1);                                    // collapsed to a single leaf
    std::cout << "  [PASSED] Delete To Empty" << std::endl;
}

// -------------------------------------------------------------------- CP5b
void test_oracle_mixed_workload() {
    std::cout << "Running test_oracle_mixed_workload..." << std::endl;
    Tree t;
    std::map<int, std::string> oracle;          // the reference implementation
    std::mt19937 rng(1234);
    const int OPS = 100'000, KEY_SPACE = 2'000; // small space → many collisions AND many deletes

    for (int op = 0; op < OPS; ++op) {
        int key = rng() % KEY_SPACE;
        if (rng() % 100 < 60) {                                    // 60% insert
            std::string v = "v" + std::to_string(op);
            bool a = t.insert(key, v);
            bool b = oracle.insert_or_assign(key, v).second;
            assert(a == b && "insert() 'was new' disagrees with std::map");
        } else {                                                   // 40% erase
            bool a = t.erase(key);
            bool b = oracle.erase(key) > 0;
            assert(a == b && "erase() 'was present' disagrees with std::map");
        }

        if (op % 1000 == 0) {                   // full check periodically — O(N) each
            t.validate();
            assert(t.size() == oracle.size());
            std::vector<int> mine;
            for (auto it = t.begin(); it != t.end(); ++it) mine.push_back((*it).first);
            std::vector<int> theirs;
            for (const auto& kv : oracle) theirs.push_back(kv.first);
            assert(mine == theirs && "full ordered scan diverged from std::map");
        }
    }
    std::cout << "  [PASSED] Oracle Mixed Workload (100k ops)" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "       BPlusTree Unit Test Suite        " << std::endl;
    std::cout << "========================================" << std::endl;

    test_empty_tree();
    test_single_insert_find();
    test_leaf_split();
    test_root_split_to_height_3();
    test_insert_orders();
    test_separator_equal_key();
    test_range_scan();
    test_delete_to_empty();
    test_oracle_mixed_workload();

    std::cout << "\nAll BPlusTree tests passed successfully!" << std::endl;
    return 0;
}
```

---

## 4. Unit-testing the repair operations in isolation

**Do not skip this for CP5b.** All six repairs (doc 05 §4) fire from the same `repairChild`
entry point, so a bug in case 4 and a bug in case 6 produce identical symptoms in an
end-to-end test. Test each alone, with a hand-built before-state:

```cpp
void test_leaf_borrow_left_isolated() {
    std::cout << "Running test_leaf_borrow_left_isolated..." << std::endl;
    // Build EXACTLY doc 05 §4.1's before-state:
    //         parent: [50]
    //        left:[20,30,40]   node:[50]
    Tree t;
    auto* left   = t.makeLeafForTest({20, 30, 40});
    auto* node   = t.makeLeafForTest({50});
    auto* parent = t.makeInternalForTest({50}, {left, right_of(left, node)});
    left->next = node;
    t.setRootForTest(parent, /*height=*/2);

    t.leafBorrowLeftForTest(parent, 1);

    assert(keysOf(left) == std::vector<int>({20, 30}));
    assert(keysOf(node) == std::vector<int>({40, 50}));
    assert(keysOf(parent) == std::vector<int>({40}));   // ← separator UPDATED (doc 05 §4.1)
    t.validate();
    std::cout << "  [PASSED] Leaf Borrow Left (isolated)" << std::endl;
}
```

This needs test-only accessors. Rather than making everything public, add one friend
declaration guarded by the same macro as the validator:

```cpp
#ifdef BPTREE_VALIDATE
    friend struct BPlusTreeTestAccess;   // test-only construction & repair invocation
#endif
```

Write one of these per repair case, asserting the exact after-state from doc 05 §4.x's
diagram. Six tests, ~25 lines each. **They are what makes CP5b tractable** — without them
you're bisecting six interacting cases through a random workload.

---

## 5. Why the `std::map` oracle is the strongest test you have

`test_oracle_mixed_workload` is worth more than every other test combined, for a reason worth
internalising beyond this project:

- **It tests behaviour, not implementation.** No knowledge of splits or merges. It says "this
  should behave like a sorted map," which is the actual specification.
- **It generates cases you wouldn't think of.** 100,000 random ops over a 2,000-key space
  produces deep merge cascades, root collapses, borrow-then-merge sequences, and
  delete-then-reinsert-the-separator — combinations no hand-written test enumerates.
- **It's reproducible.** Fixed seed → identical sequence → when it fails at op 47,231, it
  fails there every time. Shrink by replaying with `OPS = 47232` and printing the tree at
  each of the last few ops.
- **`validate()` localises what the oracle detects.** The oracle says *something* is wrong;
  the validator says *which invariant*, and doc 04 §9 / doc 05 §10 map that to a function.
  That pairing is the entire debugging loop.

**Tuning it:** small `KEY_SPACE` relative to `OPS` is deliberate — it forces high delete
success rates and keeps the tree small so merges and root collapses actually fire. With
`KEY_SPACE = 1,000,000` the tree only ever grows and you'd never exercise delete's hard paths
at all. If a run passes too easily, shrink `KEY_SPACE` further (try 50) rather than raising
`OPS`.

---

## 6. Edge cases that break real implementations

Write one test per row. Every one of these has bitten a real B+Tree.

| # | Case | What it catches |
|---|---|---|
| 1 | Insert into an empty tree | Null-root path in `insert` |
| 2 | Delete the only key | Root-leaf underflow must be *allowed*, not repaired |
| 3 | Delete every key, then insert again | Tree must be reusable after emptying |
| 4 | Insert `N` ascending, then delete ascending | Repeated leftmost-child merges — the `j` normalisation (doc 05 §7c) |
| 5 | Insert `N` ascending, then delete **descending** | Repeated rightmost merges — the mirror case; different code path |
| 6 | Delete a key that is currently a separator | The §6 non-repair (doc 05 §6) |
| 7 | Insert exactly `LEAF_MAX` keys | Full-but-not-overfull; `>` vs `>=` (doc 04 §2) |
| 8 | Insert exactly `LEAF_MAX + 1` keys | The first split, at the exact threshold |
| 9 | `scan(lo, hi)` with `lo > hi` | Must return empty, not loop or crash |
| 10 | `scan` spanning every leaf | Chain integrity end-to-end (I7) |
| 11 | Duplicate insert on an existing key | Overwrites; `size()` unchanged |
| 12 | `erase` of an absent key | Returns `false`; tree unmodified |
| 13 | Iterate a tree with an empty leaf (lazy delete) | `operator++` must *skip* empty leaves (doc 03 §7) |
| 14 | Copy a tree, mutate the copy | Deep copy; original unchanged (doc 06 §5) |
| 15 | Move a tree, then use the moved-from one | Must be a valid empty tree |
| 16 | `t = t;` (self-assign) | The self-assignment guard (doc 06 §5) |
| 17 | `Key = std::string` with a **case-insensitive** comparator | `eq()` derived from `less` only (doc 03 §3) |
| 18 | `bulkLoad` with `N = k·LEAF_MAX + 1` | The last-node underflow fix-up (doc 07 §8) |
| 19 | `scanPrefix` where the prefix ends in `0xFF` | The carry loop (doc 03 §6) |
| 20 | `LEAF_MAX = 2, INTERNAL_MAX = 3` | Minimum legal capacities; splits at the boundary of the proofs |

Case 20 is a cheap force multiplier: instantiate the **entire suite** at `<2,3>` as well as
`<4,4>` and `<64,64>`. Minimum capacities make every occupancy proof from doc 04 §3 and doc 05
§3 run at its boundary, so any off-by-one in those formulas fires immediately.

```cpp
template <int L, int I> void runAllTests(const char* label) { /* every test, parameterised */ }

int main() {
    runAllTests<2, 3>("minimum capacities");
    runAllTests<4, 4>("small (hand-traceable)");
    runAllTests<64, 64>("production-ish");
}
```

---

## 7. Build integration

Add to `internal/kernal/core/datastructures/tests/run_test.sh`, matching your existing
targets:

```bash
# --- correctness: validation on, sanitizers on, no optimisation -------------
g++ -std=c++20 -Wall -Wextra -Wsign-compare -g -O0 \
    -DBPTREE_VALIDATE -fsanitize=address,undefined \
    bplustree_test.cpp -o bplustree_test.exe && ./bplustree_test.exe

# --- performance: validation OFF, optimised (doc 07 §2) ---------------------
g++ -std=c++20 -O2 -DNDEBUG bplustree_bench.cpp -o bplustree_bench.exe && ./bplustree_bench.exe
```

**Two targets, always.** Benchmarking the validated build is doc 07 §2's mistake (a); running
correctness tests without validation defeats the point of having written it.

> **On MinGW/Windows:** ASan support is patchy. If `-fsanitize=address` doesn't link, use
> `-fsanitize=undefined` alone (which usually works), and run the ASan build under WSL or a
> Linux container in CI. Not having ASan locally is a reason to lean harder on the validator
> and the oracle, not a reason to skip memory testing.

---

## 8. Debugging playbook

When a test fails, in this order — resist the debugger until step 4:

1. **Read the assert message.** The validator names the invariant.
2. **Look up the invariant** in doc 02 §5's table → it names the likely function.
3. **`printTree()` before and after the failing operation.** Most bugs are visible in the
   shape: a node with the wrong child count, a leaf at the wrong depth, a missing `next`.
4. **Shrink.** With the oracle's fixed seed, replay to the failing op and reduce: fewer ops,
   smaller `KEY_SPACE`, `<2,3>` capacities. A 5-key repro beats a 50,000-op one.
5. **Re-read the relevant section's diagram** and hand-trace it against your `printTree`
   output. This finds it more often than stepping through does, because the bug is almost
   always a wrong index, not a wrong control flow.
6. **Check the failure-mode tables** — doc 04 §9 and doc 05 §10 — for your exact symptom.

**Top five bugs, in the order people hit them:**

1. `lower_bound` in internal routing → separator-equal keys unfindable (doc 03 §2)
2. Forgotten `next` relink in `splitLeaf` → scans return partial results (doc 04 §3)
3. `mid` vs `mid+1` in `splitInternal`'s child range → I2 (doc 04 §4)
4. Separator not updated after a leaf borrow → keys unfindable (doc 05 §4.1)
5. Internal merge discarding the separator instead of pulling it down → I2 (doc 05 §4.6)

---

## 9. You're done when

- [ ] `validate()` runs after every op in a 100,000-op mixed workload, and passes
- [ ] The `std::map` oracle agrees on every operation's return value and on the full ordered scan
- [ ] The whole suite passes at `<2,3>`, `<4,4>`, and `<64,64>`
- [ ] ASan and UBSan report nothing
- [ ] Compiles clean with `-Wall -Wextra -Wsign-compare`
- [ ] All 20 edge cases in §6 have a test
- [ ] Benchmark numbers for `N = 10³..10⁷` are recorded in a comment at the top of the header
- [ ] You can explain the copy-up/push-up asymmetry and the leaf-discard/internal-pull-down
      asymmetry **without looking them up**

That last box is the real one. The tests prove the code works; that box proves *you* work.

---

## 10. Where to go next

- **FST / trie term index** — what Lucene actually uses for the term dictionary (frontier doc
  04). Smaller than a B+Tree and supports automaton intersection for fuzzy and regex queries.
  Attempt this only after the B+Tree, because it solves a problem you'll now understand.
- **LSM-tree** — your engine's write path is already LSM-shaped (buffer → flush immutable
  segment → background merge). Doc 08 §9's read-only B+Tree *is* the SSTable. Reading up on
  LSM will reframe work you've already done.
- **Concurrency** — latch crabbing (lock the child, then release the parent) is the standard
  B+Tree concurrency protocol, and it's the reason doc 04 §2's School B (preemptive splits)
  exists. Pairs with your `concurrency.md`.
- **Prefix/front compression in leaves** — doc 07 §7.2, best done together with the page
  format in doc 08.
- **Snapshot reads via copy-on-write** — doc 08 §6 Level 2. LMDB in a weekend, roughly.
