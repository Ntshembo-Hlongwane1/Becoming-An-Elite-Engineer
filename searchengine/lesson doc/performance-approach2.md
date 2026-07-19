# Approach 2 — Building & Storing the Index in Parallel (from absolute zero)

> **What this document is.** A complete, self-contained explanation of the *write / build side* of a search engine at scale: how to construct the inverted index across many CPU cores without corrupting it, why the obvious parallel attempt *crashes*, how to make shared state safe, why you never get a clean N× speedup, and how to persist the finished index so you don't rebuild it on every run. It assumes you can program and nothing else — no concurrency background, no operating-systems course.
>
> This is the sibling of `performance-approach1.md`. That document shaped the index for fast *reads*; this one builds and stores it. Real engines split the **indexer** (this doc) from the **searcher** (approach1) because the two have opposite pressures: the searcher wants compact, sorted, query-friendly structures; the indexer wants throughput and safe concurrency. You're drawing that same line.
>
> **Prerequisite:** `complexity-and-measurement.md` for the cost language, and ideally `performance-approach1.md` so you know the *target* structure (integer doc IDs, `(docID, freq)` postings) this build produces.

---

## Part 0 — The problem, stated plainly

Two facts about your current build:

1. **It runs on one core.** `FileReader.Tokenize` is called in a plain loop, one file after another. Your machine has 8 or 16 cores; 7 to 15 of them sit idle while indexing takes minutes. (You watched the *generator* take 4m24s to write 100k files; reading and tokenizing them serially is the same order of magnitude.)
2. **It is thrown away on exit.** The kernel rebuilds the entire index in `StartAll` *before the first query*, every single run. Restart the program → pay minutes again. The index exists only in RAM.

So the write side has two walls (from `performance.md`):

- **Wall 3 — Concurrency:** use all the cores. But the instant you do, you discover *sharing a map across goroutines is unsafe* and the program crashes. Safety, not just parallelism, is the lesson.
- **Wall 4 — Durability/IO:** build once, save, and load on startup — turning a minutes-long cold start into a millisecond warm start. And confront the filesystem reality of 100k tiny files.

We'll take concurrency first (Parts 1–6), then persistence (Part 7).

---

## Part 1 — Concurrency vs parallelism (the distinction that matters)

Two words people use interchangeably and shouldn't:

- **Concurrency** is a *structuring* idea: your program is composed of independent tasks that *can* make progress without waiting on each other. It's about *dealing with* many things at once.
- **Parallelism** is an *execution* fact: tasks literally run *at the same instant* on different CPU cores. It's about *doing* many things at once.

You can have concurrency without parallelism (many tasks taking turns on one core) and you need concurrency *to get* parallelism (you must express the work as independent tasks before the runtime can spread them across cores).

Indexing is a near-perfect fit because the files are **independent**: tokenizing `redis-000000.txt` needs nothing from `kafka-000001.txt`. Work that has no dependencies between items is called **embarrassingly parallel** — the easiest and most rewarding kind to speed up. The *only* thing the files share is the destination: the single index they all write into. That shared destination is the entire difficulty, and the entire lesson.

### Go's tool: the goroutine

A **goroutine** is a function running concurrently, started with `go f()`. Goroutines are cheap — a few KB of stack, grown as needed — so you can have thousands. The Go runtime multiplexes them onto a pool of OS threads sized by `GOMAXPROCS` (default: number of CPU cores). You write concurrent *structure*; the runtime supplies the parallel *execution*.

> **Cheap is not free.** "Thousands of goroutines" tempts you to spawn one per file. Part 2 shows why that specific move backfires — not because of the goroutines themselves, but because of what they all reach for and what they each open.

---

## Part 2 — The naive attempt, and the crash you must witness

Here is the obvious parallelization. **Run it. Watch it die.** Feeling this failure is why the fix makes sense.

```go
// THE WRONG WAY — a goroutine per file, all writing one shared map.
func buildIndexBroken(files []string, store *Store) {
    var wg sync.WaitGroup
    for _, f := range files {
        wg.Add(1)
        go func(name string) {
            defer wg.Done()
            words := tokenizeFile(name)
            for _, w := range words {
                store.index[w] = append(store.index[w], ...) // ← shared map, concurrent writes
            }
        }(f)
    }
    wg.Wait()
}
```

Run it and Go halts the entire program — not a recoverable error, a hard crash:

```
fatal error: concurrent map writes
```

### Why this happens (and why it's *good* that Go crashes)

A Go `map` is **not safe for concurrent writes**. Internally a map write may reorganize buckets (grow, rehash); if two goroutines do that at the same time they corrupt the structure. Many languages let this happen silently and you get mysterious wrong data later. Go *detects* concurrent map access and **deliberately crashes immediately**, because a loud crash now is infinitely better than silent corruption you discover in production. The crash is a feature.

This is a **data race**: two goroutines accessing the same memory at the same time, at least one writing, with no coordination. Go ships a race detector to find these:

```bash
go run -race .      # or: go test -race ./...
```

`-race` instruments memory accesses and reports the exact two lines that raced, even when the timing didn't happen to crash this run. **Run every concurrent change under `-race`** — races are timing-dependent and may hide for a thousand runs, then corrupt the thousand-and-first.

### The second, quieter failure: file descriptors

Even setting the map aside, "a goroutine per file" means up to 100,000 goroutines each calling `os.Open` at once. An OS limits how many files a process may hold open simultaneously (often ~1024 by default). You'll hit:

```
too many open files
```

Both failures point at the same cure: **don't spawn unbounded work, and don't share raw mutable state.** That cure is the worker pool plus a safe-combine strategy.

---

## Part 3 — The worker pool (bounded fan-out, fan-in)

The worker pool is *the* pattern for "apply the same work to many items, in parallel, without overwhelming the machine." A **fixed** number of worker goroutines (say, one per core) pull jobs from a channel until the work runs out.

```go
func buildIndexParallel(files []string, workers int) *Store {
    jobs := make(chan string, workers*4)   // filenames to process (bounded buffer)
    results := make(chan *partialIndex)     // each worker's finished partial index
    var wg sync.WaitGroup

    // ── FAN-OUT: start N workers ──
    for w := 0; w < workers; w++ {
        wg.Add(1)
        go func() {
            defer wg.Done()
            local := newPartialIndex()       // this worker's OWN index — no sharing
            for name := range jobs {         // pull until jobs is closed & drained
                words := tokenizeFile(name)
                local.add(name, words)       // safe: local is private to this goroutine
            }
            results <- local                 // hand the finished partial back
        }()
    }

    // ── PRODUCER: feed filenames, then close so workers can finish ──
    go func() {
        for _, f := range files {
            jobs <- f
        }
        close(jobs)                          // signals "no more work"
    }()

    // ── closer: when all workers are done, close results so the merge loop ends ──
    go func() { wg.Wait(); close(results) }()

    // ── FAN-IN: merge all partials into one final index (Part 4) ──
    final := newStore()
    for p := range results {
        final.merge(p)
    }
    return final
}
```

The vocabulary, made concrete:

- **Fan-out:** one stream of jobs split across N workers. N is *fixed* (bounded) → at most N files open at once → no FD exhaustion. The channel's buffer smooths the hand-off.
- **Fan-in:** many workers' outputs combined back into one. Here, partial indexes merged into the final.
- **`close(jobs)`** is how workers learn there's no more work: `for name := range jobs` exits when the channel is closed and drained. Forgetting to close it → workers block forever → **deadlock**.
- **`WaitGroup`** counts active workers; `wg.Wait()` blocks until every `Done()` has fired. The separate closer goroutine waits, then closes `results`, so the fan-in `range` terminates.

Why this shape avoids Part 2's crash: **each worker writes only its own `local` index.** No two goroutines touch the same map. The only combination happens in the single-threaded fan-in loop. You traded "everyone shares one map (unsafe)" for "everyone has their own, merged at the end (safe)."

> You have already seen this exact skeleton — the corpus generator (`gencorpus/main.go`) used a worker pool with a `jobs` channel and a `WaitGroup` to *write* 100k files. Now you use it to *read and index* them. Same pattern, opposite direction.

---

## Part 4 — Making the combine safe: three strategies

The workers must eventually contribute to one index. There are three classic ways, and you should understand the trade-offs even though one is usually best.

### Strategy A — One shared map behind a Mutex

A **mutex** (mutual exclusion lock) ensures only one goroutine touches the map at a time.

```go
var mu sync.Mutex
// in each worker:
mu.Lock()
store.index[w] = append(store.index[w], p)
mu.Unlock()
```

Correct and simple. But every worker contends for the *one* lock on *every* word — tens of millions of lock/unlock cycles, and workers spend much of their time waiting for each other. The lock **serializes** the very work you parallelized. Speedup is poor; sometimes it's *slower* than single-threaded because of lock overhead. Good for understanding; rarely the answer for a hot path.

### Strategy B — Sharded map (lock striping)

Instead of one map+lock, use `S` maps each with its own lock; route a term to shard `hash(term) % S`.

```go
shard := shards[fnv(term)%uint32(len(shards))]
shard.mu.Lock()
shard.index[term] = append(shard.index[term], p)
shard.mu.Unlock()
```

Now two workers writing *different* terms usually hit *different* shards → different locks → no contention. Contention drops by ~S×. This is how Go's own `sync.Map` and many concurrent hash maps work internally. More complex, much better scaling than a single mutex.

### Strategy C — Per-worker partial index + final merge (recommended)

What Part 3's code already does: each worker builds a **completely private** index (zero locks, zero contention during the hot loop), and a single goroutine **merges** the partials at the end.

```go
func (s *Store) merge(p *partialIndex) {
    for term, postings := range p.index {
        s.index[term] = append(s.index[term], postings...)
    }
    // after all merges: sort each postings list by DocID (approach1 Part 2 needs this)
}
```

- **During the parallel phase:** no shared state at all → maximum parallelism, no locks, no races.
- **The cost:** a serial merge at the end, and temporarily more memory (the partials plus the final). The merge is `O(P)` — touches each posting once — and is usually a small fraction of total build time.
- **Doc-ID caveat:** if each worker assigns its own doc IDs starting at 0, they'll collide at merge. Either give each worker a disjoint ID range, or assign final IDs *during* the single-threaded merge (clean and simple). Then sort each postings list once. (See approach1 Part 8's "doc-ID instability" pitfall.)

**Why C usually wins:** the hot loop — tokenizing and tallying tens of millions of words — runs with *no synchronization whatsoever*. Synchronization is only paid once, at merge, off the hot path. A, B, and C are all correct; C moves the coordination to the cheapest possible place. Implement C, but know A and B exist and *why* they're worse here.

---

## Part 5 — Why you never get N× speedup

You have 8 cores. You will **not** get 8× faster. Understanding why keeps you from chasing impossible numbers.

### Amdahl's Law

Any program has a part that *can* be parallelized and a part that *can't* (the serial part). If a fraction `s` of the work is irreducibly serial, then no matter how many cores `c` you throw at it:

```
                    1
speedup  =  ───────────────────
             s  +  (1 - s)/c
```

As `c → ∞`, speedup → `1/s`. If even 10% of your work is serial (`s = 0.1`), the *maximum possible* speedup is 10×, ever, with infinite cores. In your indexer the serial parts are: reading the directory, feeding the jobs channel, and **the final merge** (Strategy C). Those cap your speedup no matter how many workers you add. The merge is the price of avoiding locks — a deliberate, usually-worth-it trade.

### This workload is IO-bound, not CPU-bound

The deeper reason: **indexing 100k tiny files spends much of its time waiting on the disk, not computing.** Tokenizing a 250-byte file is microseconds of CPU; *opening* and *reading* it is a syscall that may wait on the storage device. When the bottleneck is IO, adding CPU workers past a point does nothing — they all queue behind the disk. This is **IO-bound** work; its opposite is **CPU-bound** (limited by computation). The cure for IO-bound work isn't more cores — it's *fewer, larger* IO operations (Part 6) and enough concurrency to keep the disk's queue full (often only a handful of workers).

> **The practical rule:** measure speedup as you vary worker count (4, 8, 16, 32). You'll see it climb, then *flatten* (and maybe dip as overhead grows). The flattening point reveals your real bottleneck. Past it, more workers is cargo-cult parallelism. *Measure, don't assume* — exactly the discipline from `complexity-and-measurement.md`.

---

## Part 6 — The filesystem reality of 100,000 tiny files

Scale exposes the operating system, not just your algorithm. Three concrete realities the corpus made you confront:

### Per-file syscall overhead dominates

Each document is ~250 bytes of text. But processing it costs an `os.Open` (syscall), one or more `read`s (syscall), and a `close` (syscall) — plus directory bookkeeping. The *fixed cost per file* dwarfs the *cost of the bytes*. At 100k files, you pay that fixed cost 100,000 times. This is why "many small files" is a recognized performance anti-pattern, and why real engines pack many documents into a few large **segment files**: one open, one big sequential read, thousands of documents — turning 100k expensive small reads into a handful of cheap large ones.

### `os.ReadDir` materializes everything at once

```go
entries, err := os.ReadDir(filepath.Join(wd, f.dir))  // returns ALL 100k entries
```

This builds a slice of 100,000 `DirEntry` values before you process a single file — one large allocation and a latency spike at startup. For very large directories, streaming with `File.ReadDir(n)` in batches lets you start working before the whole listing is in memory. Minor at 100k, decisive at millions.

### Cluster slack and the waste you measured

You saw it directly: ~25 MB of actual text consumed **185 MB on disk**. Why? A filesystem allocates space in fixed **clusters** (commonly 4 KB on NTFS). A 250-byte file still occupies a whole 4 KB cluster — ~3.75 KB wasted per file, ×100k ≈ 375 MB of slack. Segment files (many docs per file) reclaim almost all of it. This is a property of the *storage layer*, invisible in your code, and only learnable by looking at real `du` output on real scale.

### The small waste worth fixing now

`FileReader.Tokenize` calls `os.Getwd()` **once per file** (file_reader.go:132) — 100,000 identical syscalls for a value that never changes. Hoist it out of the loop. Invisible at 17 files; 100,000 pointless syscalls at scale. A clean example of "constant-factor waste that only a profiler or a careful reading reveals."

---

## Part 7 — Persistence: build once, load forever

The last wall. Right now the index dies with the process and is rebuilt — minutes — on every start. A real engine **builds once, persists, and loads**. Startup goes from minutes to milliseconds.

### The decision

```go
func startup() *Store {
    if fileExists("index.gob") {
        s := newStore()
        s.LoadIndex("index.gob")   // milliseconds
        return s
    }
    s := buildIndexParallel("./data", runtime.NumCPU())  // minutes, ONCE
    s.SaveIndex("index.gob")
    return s
}
```

### Serialization: turning structures into bytes and back

**Serialization** (or *marshalling*) is converting in-memory structures into a byte stream you can write to disk; **deserialization** reverses it. Two routes:

- **`encoding/gob` (start here).** Go's native binary format. It serializes your `documents []string` and `index map[string][]Posting` almost for free:

```go
func (s *Store) SaveIndex(path string) error {
    f, err := os.Create(path)
    if err != nil { return err }
    defer f.Close()
    enc := gob.NewEncoder(f)
    if err := enc.Encode(s.documents); err != nil { return err }
    return enc.Encode(s.index)
}
func (s *Store) LoadIndex(path string) error {
    f, err := os.Open(path)
    if err != nil { return err }
    defer f.Close()
    dec := gob.NewDecoder(f)
    if err := dec.Decode(&s.documents); err != nil { return err }
    return dec.Decode(&s.index)
}
```

Simple, correct, good enough to make startup instant. Persist `documents` *and* `index` together so doc IDs and postings stay consistent (approach1 Part 8's stability pitfall).

- **Custom binary format (the stretch).** A hand-rolled layout — varint-encoded, delta-compressed doc IDs (store gaps between sorted IDs, which are small numbers that pack tightly) — is smaller and faster to load than gob. This is what Lucene does and why its indexes are compact. **Measure both** before deciding: gob is often plenty, and "smaller/faster" must be *demonstrated*, not assumed. Premature custom formats are a classic waste.

### Why this is the gateway to more

Once the index is a file, new capabilities open up: **memory-mapping** it (let the OS page it in on demand, so the index needn't fully fit in RAM), **incremental indexing** (append new documents without rebuilding), and **segment merging** (the indexer/searcher architecture in full). You don't need these yet — but persistence is the door to all of them, which is why it's the final exercise.

---

## Part 8 — Pitfalls (the bugs you *will* hit)

**Not running `-race`.** A data race may not crash on the run you tested. `go test -race` / `go run -race` is mandatory for concurrent code; treat any race report as a release blocker, not a warning.

**Deadlock from a channel you never close.** `for x := range ch` blocks forever if `ch` is never closed. Close `jobs` after the producer finishes; close `results` after `wg.Wait()`. A program that hangs at the end of indexing is almost always an unclosed channel.

**Closing a channel from the wrong place (or twice).** Close a channel from the *sender* side, exactly once. Closing from a receiver, or twice, panics. The producer closes `jobs`; a dedicated `wg.Wait(); close(results)` goroutine closes `results`.

**WaitGroup misuse.** `wg.Add` must happen *before* the goroutine starts (not inside it, where it may race with `Wait`). `defer wg.Done()` at the top of the worker guarantees it fires even on early return.

**Doc-ID collisions across workers.** Per-worker partials that each start IDs at 0 will alias different documents to the same ID at merge. Assign final IDs during the serial merge, or hand each worker a disjoint range (Part 4, Strategy C caveat).

**Forgetting to sort after a parallel merge.** Parallel construction destroys the natural ascending order approach1's merge intersection depends on. Sort each postings list once after merging, and have a test assert sortedness.

**Optimizing worker count by guessing.** More workers past the IO bottleneck does nothing or hurts (Part 5). Benchmark across worker counts; pick the knee of the curve.

---

## Part 9 — When this matters, and where you've seen it

The worker-pool / fan-out-fan-in pattern is the backbone of virtually all data-processing systems: web servers (a pool handling requests), MapReduce and Spark (map = fan-out, reduce = fan-in), image/video pipelines, ETL jobs, web crawlers. The "private state per worker, merge at the end" strategy is *exactly* MapReduce's shape, and *exactly* how Lucene builds index segments in parallel then merges them. The persistence story — serialize, load, memory-map, merge segments — is the architecture of every production search engine and most databases.

The transferable lessons beyond search:
- **Sharing mutable state is the hard part of concurrency, not the parallelism.** Prefer "don't share" (private state + merge) over "share carefully" (locks) whenever you can.
- **Know your bottleneck's *kind*** — CPU-bound wants more cores; IO-bound wants fewer, larger operations. Throwing cores at IO-bound work is wasted effort.
- **Durability is a feature, not an afterthought.** "Rebuild from scratch every time" is fine at 17 files and unacceptable at 100k. Persisting computed work is one of the highest-leverage moves at scale.

---

## Part 10 — Self-implementation blueprint (do this from a blank file)

1. **Cause the crash on purpose.** Write the goroutine-per-file version that shares one map. Run it under `-race`. Read the `concurrent map writes` / race report. *Understand it* before continuing.
2. **Write the worker pool skeleton.** Fixed N workers, a `jobs` channel, a producer that closes `jobs`, a `WaitGroup`. No index yet — just have workers count files. Confirm it terminates (no deadlock) and processes every file exactly once.
3. **Give each worker a private partial index.** Tokenize into `local` (reuse approach1's `(docID, freq)` structure). Send partials on a `results` channel; close it via `wg.Wait()` in a closer goroutine.
4. **Write the serial merge (fan-in).** Combine partials, assign final doc IDs, sort each postings list. Assert sortedness in a test. Run the whole thing under `-race` — it must be clean.
5. **Measure speedup.** Time serial (approach baseline) vs parallel. Vary workers (4/8/16/32); plot or tabulate the times; find where it flattens. Explain the flattening (Amdahl + IO-bound).
6. **Fix the IO constants.** Hoist `os.Getwd()` out of the per-file path. (Stretch: batch directory reads; experiment with reading docs in larger chunks.)
7. **Add persistence.** `SaveIndex`/`LoadIndex` with `gob`. Wire the load-or-build-then-save decision into startup. Time cold start (build) vs warm start (load); record both.
8. **(Stretch) Custom binary format.** Delta+varint encode sorted doc IDs. Measure size and load time against gob. Keep it only if the numbers justify it.

When all pass and you have serial-vs-parallel and cold-vs-warm numbers written down, the write side scales: the engine builds the index across all cores, safely, and starts instantly from disk.

---

## Part 11 — Self-check (answer from memory)

- What is the difference between concurrency and parallelism? Why is indexing "embarrassingly parallel," and what is the one thing the files share?
- Exactly what causes `fatal error: concurrent map writes`, and why is it *good* that Go crashes instead of continuing?
- What is a data race, and what command finds one even when it didn't crash this run?
- Besides the map, what second failure does "a goroutine per file" cause, and why?
- Draw the worker-pool: fan-out, jobs channel, workers, results channel, fan-in. Who closes `jobs`, who closes `results`, and what breaks if you forget either?
- Compare the three safe-combine strategies (mutex / sharded / per-worker-merge). Why does the per-worker-merge usually win, and what does it cost?
- State Amdahl's Law in words. If 10% of the work is serial, what's the most speedup you can ever get? What are the serial parts of your indexer?
- What does "IO-bound" mean, why is indexing 100k tiny files IO-bound, and why doesn't adding cores help past a point?
- Why did 25 MB of text take 185 MB on disk? What do segment files fix, and how?
- What is serialization? Why persist `documents` and `index` together? What does persistence turn a cold start into, and what capabilities does it unlock next?

If those come without flipping back, you understand the write side at the level where you could build a parallel, persistent indexer blind — and you'll recognize the worker-pool / fan-in / persist pattern in every data system you ever touch. Together with `performance-approach1.md`, you can now take a search engine from "correct at 17 files" to "fast, compact, safe, and instant-starting at 100,000" — and prove every improvement with a number.
