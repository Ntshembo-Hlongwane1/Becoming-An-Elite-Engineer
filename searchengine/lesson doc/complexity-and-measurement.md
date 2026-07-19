# Reading Cost: Complexity & Measurement — from absolute zero

> **What this document is.** A complete, self-contained guide to the one skill every later performance lesson assumes: the ability to *see* what an operation costs, predict how that cost grows as your data grows, and then *measure* it for real instead of guessing. It assumes you can program and nothing else — no algorithms course, no Big-O background, no profiling experience.
>
> This is the **`ebnf-notation.md` of the performance series.** Just as you cannot read the parsing approach docs without first being able to read grammar notation, you cannot read `performance-approach1.md` and `performance-approach2.md` without first being able to read *cost*. Every claim those docs make — "this is O(n)", "this allocates once per token", "the heap stack won't overflow" — is written in the language taught here. Learn this language first and the rest becomes transparent.
>
> We teach it on tiny, neutral examples and then point every idea straight at your real `store.go` and `file_reader.go`.

---

## Part 0 — The problem, stated plainly

You generated 100,000 documents. Indexing them now takes minutes. Your gut says "that's slow."

**"Slow" is not a measurement.** It is a feeling. You cannot fix a feeling. Before you touch a single line of the engine, you need two precise tools:

1. A way to **predict** how cost grows *before* you run anything — so you can tell, by reading code, that one approach will die at 100k and another won't. That tool is **complexity analysis** (Big-O).
2. A way to **measure** actual cost *after* you run it — so you know whether your fix helped, by how much, and whether it secretly made memory worse. That tool is **benchmarking and profiling**.

The single most common mistake engineers make at scale is optimizing by *vibe*: rewriting code they *feel* is slow, never measuring, and often making it worse. The discipline this document teaches is the opposite: **predict with Big-O, confirm with measurement, never guess.**

Here is the trap in one picture. At small `n`, everything is fast and all designs look equal:

```
n = 17 documents      → every approach finishes in microseconds. You learn nothing.
n = 100,000 documents → the bad designs take minutes or crash; the good ones stay fast.
                        The DIFFERENCE between designs only becomes visible at scale.
```

That is *why* you grew the corpus. Scale doesn't just make things slower — it makes the *differences between designs* visible. This document teaches you to see those differences on paper, before they bite.

---

## Part 1 — Big-O: counting growth, not seconds

### What Big-O actually is

Big-O notation answers exactly one question:

> **As the input gets bigger, how does the amount of work grow?**

It does **not** measure seconds. It does not care whether your laptop is fast or whether Go is faster than Python. It measures the *shape of the growth curve* — how work scales when `n` (the size of the input) scales. Seconds depend on the machine; growth shape is a property of the *algorithm itself*, and it is what decides whether you survive at 100k.

Think of it as the answer to: "if I make the input 10× bigger, what happens to the work?"

| Class | Name | If input grows 10×, work grows… | Feels like |
|-------|------|--------------------------------|------------|
| `O(1)` | constant | not at all | a hashmap lookup |
| `O(log n)` | logarithmic | by a tiny fixed step (~3 more) | binary search, a balanced tree |
| `O(n)` | linear | 10× | scanning a list once |
| `O(n log n)` | linearithmic | ~13× | a good sort |
| `O(n²)` | quadratic | **100×** | comparing every item to every other |
| `O(2ⁿ)` | exponential | astronomically | brute-forcing combinations |

### Put real numbers on it

Abstract classes don't scare anyone. Concrete operation counts do. Here is the *number of basic operations* each class implies at your two scales:

```
                 n = 10,000        n = 100,000      ratio (100k / 10k)
O(1)             1                 1                1×        ← does not care about n
O(log n)         ~13               ~17              ~1.3×     ← barely moves
O(n)             10,000            100,000          10×       ← grows with the data
O(n log n)       ~130,000          ~1,700,000       ~13×      ← a bit worse than linear
O(n²)            100,000,000       10,000,000,000   100×      ← 10 BILLION. dead.
```

Read the bottom row twice. An `O(n²)` algorithm that took a tolerable beat at 10k does **100× more work** at 100k. That is the difference between "instant" and "the program appears to hang." **The whole game at scale is staying out of the `O(n²)` row** — and knowing which of your operations secretly live there.

### Why you drop constants and lower-order terms

Big-O deliberately throws information away. If you count and get `3n² + 500n + 9000` operations, the Big-O is just **`O(n²)`**. Three rules:

1. **Drop constant multipliers.** `3n²` → `n²`. Constants depend on the machine and the exact code; growth *shape* does not. (Constants still matter in real life — Part 5 — but not for the growth class.)
2. **Keep only the fastest-growing term.** `3n² + 500n + 9000` → `n²`, because as `n` grows, `n²` dwarfs everything else. At n=100k, `n²` is 10 billion while `500n` is 50 million — the `n²` term is 200× larger and pulling away.
3. **The result names the curve, not the cost.** `O(n²)` means "work grows like the square of the input," nothing more.

> **The mental test for any code:** "If I double the input, what happens to this loop's work?" Same → `O(1)`. Doubles → `O(n)`. Quadruples → `O(n²)`. That single question, applied honestly, classifies almost everything you'll write.

---

## Part 2 — Reading the complexity of YOUR code

Theory is useless until you can point it at real code. Let's read the actual search engine.

### `Tokenize` — building the index

From `file_reader.go`, simplified:

```go
for scanner.Scan() {                 // for each LINE in the file
    words := strings.Fields(line)    // split into words
    for _, word := range words {     // for each WORD in the line
        cleaned := removePunctuation(word)
        if !isStopWord(cleaned) {
            store.UpdateSearchIndex(cleaned, docFile)
        }
    }
}
```

Let `B` = the total number of bytes (characters) in a document. The scanner reads every line once, `Fields` walks every character once, and the inner loop touches every word once. Every character is handled a constant number of times. So tokenizing one document is **`O(B)`** — linear in the document's size. You cannot beat that; you *must* read every character at least once to index it. Across the whole corpus of total size `T` bytes, building the index is **`O(T)`**. This part is already optimal in its growth class. (Its *constant factor* and its *concurrency*, however, are wide open — that's `performance-approach2.md`.)

### `isStopWord` and `UpdateSearchIndex` — the hashmap

```go
func (f *FileReader) isStopWord(word string) bool {
    _, exists := StopWords[word]      // map lookup
    return exists
}

func (s *Store) UpdateSearchIndex(key, value string) {
    s.searchIndex[key] = append(s.searchIndex[key], value)   // map lookup + append
}
```

A map lookup is **`O(1)`** — constant time, independent of how many keys the map holds. This is *the* reason search engines exist: a hashmap turns "find documents containing this word" from a scan of everything into a single constant-time jump. Hold onto that — it is the entire thesis of the project (the overview's final line: *"search is fundamentally an indexing problem rather than a scanning problem"*). The `append` is `O(1)` *amortized* (Part 3 explains that word).

### The naive multi-term search — the hidden `O(n²)`

Now the dangerous one. Suppose for `redis AND replication` you intersect two postings lists the obvious way:

```go
func intersect(a, b []string) []string {
    var out []string
    for _, x := range a {            // for each doc in a   (size n)
        for _, y := range b {        // for each doc in b   (size m)
            if x == y {
                out = append(out, x)
            }
        }
    }
    return out
}
```

Two nested loops over the lists: **`O(n × m)`**. At 10k, if `redis` and `replication` each hit ~2,500 docs, that's ~6 million comparisons — a blink. At 100k, each hits ~25,000 docs → **625 million** comparisons *per query*. That is the `O(n²)` row, and it is why a query that felt instant on the seed data will feel broken on the full corpus. The fix (sorted postings + a merge walk, dropping it to `O(n + m)`) is the centerpiece of `performance-approach1.md`. For now, the lesson is only this: **you could have predicted the disaster by reading the two nested loops — no profiler needed.**

---

## Part 3 — Amortized analysis: why `append` is "O(1)" even though it sometimes copies

You will see the phrase **"amortized O(1)"** constantly. It has a precise meaning, and `append` is the perfect teacher.

A Go slice has a length and a *capacity* (room allocated). `append` adds to the end. Usually there's spare capacity and it just writes one element — `O(1)`. But when capacity runs out, Go allocates a **bigger** backing array (commonly ~2× the old size), **copies every existing element over**, then appends. That copy is `O(n)` — expensive!

So a single `append` is *sometimes* `O(1)` and *occasionally* `O(n)`. How can we call it `O(1)`?

**Amortized analysis** asks for the *average cost per operation across a long run*, not the worst single one. Watch the growth as you append `n` items, doubling capacity each time it fills:

```
append #1   → grow to cap 1,  copy 0        (cheap)
append #2   → grow to cap 2,  copy 1
append #3   → grow to cap 4,  copy 2
append #5   → grow to cap 8,  copy 4
append #9   → grow to cap 16, copy 8
...
the copies are:  0 + 1 + 2 + 4 + 8 + 16 + ... + n/2
```

That sum of doublings is `≈ n` total copies across *all* `n` appends. Total work ≈ `n` appends + `n` copies = `2n` = `O(n)` for the whole sequence. Divide by `n` operations → **`O(1)` per append, on average.** That is "amortized O(1)": any *single* append might be pricey, but the *total* over many appends is linear, so the per-op average is constant.

> **You have already met this exact argument.** In `parsing-approach2.md` Part 11, shunting-yard is proven `O(n)` even though its inner `while` loop can pop many operators at once — because "every operator is pushed once and popped once over the whole run." Same idea: don't price the worst single step; price the whole sequence and divide. Amortized reasoning is a reusable lens, not a one-off trick.

**Why it still matters at scale despite being "O(1)":** those `O(n)` copies are real memory traffic and real garbage. A postings list that grows to 50,000 entries was reallocated and copied ~16 times on the way up, throwing away ~16 ever-larger arrays for the garbage collector to clean. At 100k docs across tens of thousands of terms, that churn is measurable. The cure — telling Go the capacity up front with `make([]T, 0, expectedSize)` — is a constant-factor win that amortized Big-O *hides*. This is your first hint that **Big-O is necessary but not sufficient.** Keep going.

---

## Part 4 — Space complexity: counting bytes, not just time

Everything above measured *time* (operations). The exact same Big-O language describes *memory*. And at scale, memory is often the wall you hit first — because running out of RAM doesn't slow you down, it *kills the process*.

### Reading the memory of your index

`searchIndex map[string][]string`. What does it cost? Let's count, because the count reveals the design flaw.

- The map has one entry per **unique term**. Call that `V` (vocabulary size). On the 100k corpus, `V` is perhaps ~1,000–2,000 distinct words.
- Each value is a `[]string` postings list. The *total* number of strings across all postings equals the total number of indexed (non-stop) word occurrences in the corpus — call it `P`. `P` is in the **tens of millions** at 100k docs.
- **Here is the flaw:** each posting is the *full filename string*, e.g. `"redis-012345.txt"` — 16 bytes of text plus a 16-byte string header (a pointer + a length, on a 64-bit machine) = ~32 bytes. And the **same filename is stored once per occurrence** (because `UpdateSearchIndex` appends on every hit, and the same term appears many times in the same doc).

So the postings memory is roughly `P × 32 bytes` of *mostly duplicated strings*. That is **`O(P)` space, with a large constant** — and a big chunk of it is pure waste: the same filename text, repeated millions of times.

### The fix is a complexity-and-constant story

Replace the filename string with an **integer document ID**. `"redis-012345.txt"` becomes `12345`, an 8-byte `int` (often less). Now a posting is 8 bytes, not 32, and identical postings are identical integers you can dedupe and sort. Same `O(P)` *growth class* — but a 4×+ smaller constant and a structure you can actually compute ranking on. This is the heart of `performance-approach1.md`. The point *here* is the skill: **you found a memory problem by counting bytes per element and multiplying by how many elements scale demands** — exactly the way you'd count operations for time.

> **Space-time tradeoffs are the texture of this whole field.** An index *is* spending memory to buy query speed — you store `Word → Documents` (more memory) so you never scan documents (less time). Almost every decision ahead is "spend memory to save time" or the reverse. Big-O lets you price both sides of the trade.

---

## Part 5 — The two things Big-O hides (and why your 100k still feels slow)

Big-O is the most important tool, and it is **not the whole truth**. Two algorithms with the *same* Big-O can differ 10–100× in real speed. Two reasons, both decisive at scale.

### 1. Constant factors and the memory hierarchy

`O(n)` means "work ∝ n," but it says nothing about the work *per item*. Reading 100k integers from one contiguous slice and reading 100k strings scattered across the heap are *both* `O(n)` — and the first is often **10× faster**, because of how real hardware fetches memory:

```
        Approx. cost to access one piece of data (rough orders of magnitude)
   CPU register / L1 cache .......... ~1        (right next to the CPU)
   L2 / L3 cache ................... ~10
   Main memory (RAM) ............... ~100       ← a "cache miss"
   SSD disk ........................ ~100,000
   Spinning disk / network ......... ~10,000,000
```

A contiguous `[]int` lives in one block of memory; the CPU prefetches the next elements into cache before you ask for them, so most accesses are ~1–10. A `[]string` is a slice of *headers* pointing to text *elsewhere* on the heap; chasing each pointer is a likely cache miss (~100). **Same Big-O, wildly different constant**, entirely because of *data layout* and *locality*. This is the deep reason "use integers, keep them contiguous, sort them" is repeated like a mantra in IR — it's not pedantry, it's the memory hierarchy.

### 2. Allocation and garbage collection

Every `string`, every slice grow, every small struct on the heap is an *allocation*. Go's garbage collector must later find and free it. Allocations are individually cheap but **collectively brutal**: tokenizing 100k documents the naive way produces *tens of millions* of short-lived string allocations, and the GC must scan all of it. This shows up as mysterious pauses and CPU spent in the runtime, not in your code. Big-O counts none of it — allocation is "O(1) per allocation" and invisible to the curve. Yet it can dominate your wall-clock time.

**The takeaway:** Big-O tells you which designs *cannot possibly* scale (avoid the `O(n²)` query). Once you're in the right class, **constants — locality and allocation — decide the winner**, and you can only see them by *measuring*. Which is the rest of this document.

---

## Part 6 — Measuring time and memory for real (Go benchmarks)

Stop guessing. Go has measurement built into the toolchain, and learning it is non-negotiable for the rest of the series.

### The benchmark

A benchmark is a function named `BenchmarkXxx` taking `*testing.B`, placed in a `_test.go` file. The framework runs your code `b.N` times, automatically increasing `b.N` until the timing is statistically stable.

```go
package store

import "testing"

func BenchmarkIntersect(b *testing.B) {
    // ---- setup: build inputs ONCE, outside the timed loop ----
    a := makePostings(25000)   // simulate a 100k-corpus postings list
    c := makePostings(25000)
    b.ResetTimer()             // <-- don't count the setup above

    for i := 0; i < b.N; i++ {
        _ = intersect(a, c)    // the ONE thing under test
    }
}
```

Three rules that beginners always break:

1. **Put setup outside the timed region.** Build your test data before the loop and call `b.ResetTimer()` so the framework doesn't blame your function for setup cost.
2. **The framework chooses `b.N`, not you.** Never write `for i := 0; i < 1000; i++`. Loop exactly `b.N` times so the framework can scale the run to get a stable measurement.
3. **Use the result.** Assign to `_ =` (or accumulate) so the compiler can't "optimize away" your function as dead code and report a fake 0 ns.

Run it:

```bash
go test -bench=Intersect -benchmem ./src/internal/store
```

Output looks like:

```
BenchmarkIntersect-8     123    9,512,340 ns/op    4,202,496 B/op    3 allocs/op
                     │      │          │                 │              │
          GOMAXPROCS─┘      │          │                 │              └ heap allocations per call
        (cores used)        │          │                 └ bytes allocated per call
   times it ran the loop ───┘          └ nanoseconds per call  ← the headline number
```

`-benchmem` is the magic flag: **`B/op` and `allocs/op` expose the memory cost Big-O hides** (Part 5). A change that cuts `ns/op` but triples `allocs/op` may lose under real load when the GC catches up. Always watch both columns.

### The golden loop

```
1. Write a benchmark for the operation you suspect.
2. Run it. WRITE THE NUMBER DOWN (ns/op, B/op, allocs/op).
3. Make ONE change.
4. Run it again. Compare to the written-down number.
5. Keep the change only if it actually helped. Repeat.
```

"WRITE THE NUMBER DOWN" is not a joke. Optimization without a recorded baseline is just rearranging code and hoping. Every exercise in `performance.md` makes you do this.

### Ad-hoc timing (when a benchmark is overkill)

For a one-off "how long does indexing the whole corpus take?", `time.Now()` is fine:

```go
start := time.Now()
buildIndex("./data")
fmt.Printf("indexed in %s\n", time.Since(start))
```

That is exactly what the corpus generator printed (`Done. Wrote 100000 files in 4m24s`). It's a coarse stopwatch — good for whole-program timing, useless for comparing two implementations of a hot function (use a benchmark for that, so `b.N` averages out noise).

---

## Part 7 — Profiling: finding *where* the cost actually is

Benchmarks tell you a function is slow. **Profiling tells you *which part* is slow** — and the answer is almost always somewhere you didn't expect. The iron law of optimization:

> **Don't optimize what you *think* is slow. Profile, then optimize what *is* slow.**

Most programs spend ~90% of their time in ~10% of the code. Profiling finds that 10% so you don't waste days speeding up code that runs 0.1% of the time.

### CPU profile — where the time goes

```bash
go test -bench=BuildIndex -cpuprofile=cpu.out ./src/internal/store
go tool pprof cpu.out
```

Inside `pprof`, the command `top` lists the functions where the program spent the most CPU. You will often discover the time is in something boring — `strings.ToLower`, `regexp`, the GC, `append` reallocations — not your "clever" code. That discovery is the entire value: it redirects your effort to where it pays.

### Heap profile — where the memory goes

```bash
go test -bench=BuildIndex -memprofile=mem.out ./src/internal/store
go tool pprof mem.out
```

Same `top`, but now ranked by allocation. This is how you'd *prove* the duplicate-filename-string problem from Part 4: the heap profile would show a mountain of allocation attributed to your postings strings, confirming the byte-counting you did on paper. **Paper prediction + profiler confirmation = certainty.**

### The shape of the discipline

```
        ┌─────────────┐  predict   ┌──────────────┐  confirm   ┌────────────┐
        │  Big-O on    │ ─────────► │  Benchmark   │ ─────────► │  Profile   │
        │  paper       │            │ (-benchmem)  │            │  (pprof)   │
        └─────────────┘            └──────────────┘            └────────────┘
         "this nested loop          "yep, 9ms/call and          "and 80% of it
          is O(n²), it'll die"       4MB allocated/call"         is in append"
                                          │                            │
                                          └────────► FIX ◄─────────────┘
                                                      │
                                                  re-measure
```

You will run this loop in every performance exercise. Predict, measure, profile, fix, re-measure. Never skip straight to "fix."

---

## Part 8 — A worked example tying it all together

Let's price the duplicate-postings problem end to end, the way you should price everything.

**Predict (Big-O + bytes).** `UpdateSearchIndex` appends a filename string on *every* occurrence of a term. The term `redis` appears ~5× in a redis document; across ~2,500 redis docs that's ~12,500 postings for one term — but only ~2,500 *distinct* documents. So ~80% of those postings are duplicates. Memory: ~12,500 × 32 bytes ≈ 400 KB for *one* term, ~80% wasted. Multiply across the vocabulary → tens of MB of pure duplication. Time: the duplicates also inflate every intersection (`O(n×m)` with a bigger `n`).

**Measure (benchmark).** Write `BenchmarkBuildIndex`. Record `B/op`. You'll see allocation far above the theoretical minimum.

**Profile (pprof heap).** `top` attributes the bulk to postings-string allocation in `UpdateSearchIndex`. Prediction confirmed.

**Fix.** Dedupe postings (store each doc once per term) and switch to integer doc IDs. Re-run the benchmark: `B/op` drops sharply, and `intersect` speeds up because its lists got shorter and became sortable.

**Re-measure.** Compare to the number you wrote down. Quantify the win ("3.1× less memory, 4× faster intersection"). *Now* you've engineered, not guessed.

Every fix in the next docs follows this exact arc. The arc *is* the discipline.

---

## Part 9 — Self-check (answer from memory before moving on)

If these come without scrolling up, you can read everything in `performance.md` and the two approach docs.

1. What does Big-O measure — and what does it deliberately *not* measure?
2. If an algorithm is `O(n²)` and you grow the input 10×, how much more work is there? Why is that the row to avoid at scale?
3. Why do you drop constants and lower-order terms when finding the Big-O? Give the mental test you apply to a loop to classify it.
4. What is the Big-O of a single map lookup, and why is that fact the entire reason a search index exists?
5. Read the naive `intersect` (two nested loops): what's its complexity, and roughly how many comparisons does it do per query at 100k? Why does it feel fine at 10k?
6. Explain "amortized O(1)" using `append`. Where have you seen the same amortized argument before in these lessons?
7. Estimate the memory of `map[string][]string` postings at 100k. Where is the waste, and what one change removes most of it?
8. Name the two things Big-O hides that can make two same-Big-O algorithms differ 10–100×. Give a concrete cause of each.
9. Why is a contiguous `[]int` typically much faster to scan than a `[]string` of the same length, despite identical Big-O?
10. In a Go benchmark: why must setup go before `b.ResetTimer()`, why must you loop `b.N` times (not a fixed count), and what does `-benchmem` add?
11. What does a CPU profile tell you that a benchmark does not? State the iron law of optimization.
12. Recite the five-step golden loop of measurement, and the predict→measure→profile→fix arc.

When those answers are automatic, you have the lens. Now go read `performance.md` for the map of *what* breaks at 100k and *which* concept fixes each — then `performance-approach1.md` (the read/query side) and `performance-approach2.md` (the write/build side), where every cost claim is written in the language you just learned.
