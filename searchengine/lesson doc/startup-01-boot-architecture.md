# Startup & Boot Architecture — Part 1: The Mental Model

> **This is a 5-part series on the boot/startup flow of your search engine.**
> It exists to answer three questions you asked:
>
> 1. When the program starts, the **Store** should tell the **Kernal** whether a saved
>    search index already exists — and if so, the **Engine** starts *immediately*.
> 2. If the Store says the index is empty/missing, the Kernal must **kick-start** the
>    DirectoryReader → Lexer → Parser pipeline.
> 3. When the Parser receives its **poison pill**, it must **alert the Kernal** that
>    indexing is complete, so the Kernal can start the Engine and accept queries.
>
> **These docs teach you the concepts and the C++ practices. They do NOT write the
> feature for you.** You will find design questions, skeletons with the important logic
> left blank, and "your turn" prompts. The point is that *you* build it.

### The series

| Part | File | What it covers | Your question |
|---|---|---|---|
| 1 | `startup-01-boot-architecture.md` | The systems mental model, cold vs warm start, control/data plane | Framing all three |
| 2 | `startup-02-persistence-and-warm-start.md` | Detecting & loading a saved index; how Store reports it | Q1 |
| 3 | `startup-03-conditional-orchestration.md` | The Kernal branching the boot path | Q1 + Q2 |
| 4 | `startup-04-completion-signalling.md` | Parser → Kernal "indexing done" signal | Q3 |
| 5 | `startup-05-putting-it-together.md` | The full boot state machine + a C++ practices checklist | All |

> Foundations you already have docs for — read them alongside this series:
> `ownership-and-lifecycle-part1..4.md`, `concurrency.md`, `backpressure-approach1/2.md`.
> I will reference them instead of re-teaching RAII, `unique_ptr`, atomics, and ring buffers.

---

## 1. The one idea that unlocks everything: **boot is a decision, not a sequence**

Right now your `main.cpp` does this:

```cpp
kernal.InitAll();     // init every subsystem
kernal.StartAll();    // start every subsystem, unconditionally
std::cin.get();       // wait
kernal.StopAll();     // stop every subsystem
```

`StartAll()` starts **everything, every time, in the same order**. That is a *sequence*.

What you are asking for is fundamentally different. You want the Kernal to **ask a
question first** ("is there a saved index?") and then **choose one of two boot paths**:

```
                         ┌─────────────────────────────┐
                         │  Store: does a saved index   │
                         │  exist on disk?              │
                         └──────────────┬──────────────┘
                                        │
                   ┌────────────────────┴────────────────────┐
                   │ YES (warm start)                         │ NO (cold start)
                   ▼                                          ▼
        ┌────────────────────┐              ┌──────────────────────────────────┐
        │ Load index into    │              │ Start DirectoryReader → Lexer →   │
        │ Store, then start   │              │ Parser. Build the index.          │
        │ Engine immediately  │              │ Wait for "indexing complete".     │
        └─────────┬──────────┘              └──────────────────┬───────────────┘
                  │                                            │
                  │                                            ▼
                  │                            ┌──────────────────────────────┐
                  │                            │ Parser signals Kernal: done.  │
                  │                            │ (Optionally persist index.)   │
                  │                            └──────────────┬───────────────┘
                  │                                           │
                  └───────────────────┬───────────────────────┘
                                      ▼
                         ┌──────────────────────────┐
                         │ Engine STARTED.           │
                         │ Ready for search queries. │
                         └──────────────────────────┘
```

Everything else in this series is just *how to express this diagram in idiomatic C++*.

---

## 2. Cold start vs warm start (borrow the vocabulary from real systems)

This exact pattern has a name in databases, caches, and operating systems.

- **Warm start** — the system finds pre-computed state on disk and resumes from it.
  Fast, because the expensive work (building the index) was done in a previous run.
- **Cold start** — no saved state; the system must rebuild everything from raw inputs.
  Slow, because it re-reads every document and re-tokenizes it.

Real examples you can anchor to:

| System | Warm start | Cold start |
|---|---|---|
| **Redis** | Load `dump.rdb` snapshot from disk | Empty dataset, rebuild from clients |
| **Postgres** | Replay WAL, mount existing data dir | `initdb` — create a fresh cluster |
| **Elasticsearch** | Open existing Lucene segments | Reindex documents from source |
| **A JVM** | Load AOT/JIT cache | Interpret + JIT from scratch |
| **Your engine** | Load saved `searchIndex_` | Run DirReader→Lexer→Parser |

> **Systems insight:** the *reason* warm start exists is that indexing is **expensive and
> deterministic**. If the inputs haven't changed, re-running the pipeline produces the
> same index. So you cache the result. This is the same logic as build caches, memoization,
> and CPU instruction caches — "don't recompute what you already computed."

You are not inventing a weird special case. You are implementing one of the most
fundamental patterns in systems engineering.

---

## 3. Control plane vs data plane — *who* makes the decision

Your codebase already separates two kinds of components, even if you didn't name it:

- **Control plane** — the part that *manages lifecycle and makes decisions*.
  That is your `Kernal`. It doesn't process documents; it starts, stops, and orders
  the things that do.
- **Data plane** — the parts that *do the actual work on data*.
  DirectoryReader, Lexer, Parser, Engine, and the Store's contents.

This distinction matters enormously for your three questions, because it tells you
**where each piece of logic belongs**:

| Logic | Plane | Lives in |
|---|---|---|
| "Does a saved index exist?" | data (it's about *data* on disk) | **Store** |
| "Given the answer, which boot path?" | control (it's a *decision*) | **Kernal** |
| "Build the index from documents" | data | DirReader/Lexer/Parser |
| "Indexing is finished" (the fact) | data | Parser |
| "React to indexing finishing by starting Engine" | control | **Kernal** |

Notice the pattern: **facts** are produced by the data plane; **decisions** are made by
the control plane. The Store *reports* whether an index exists — it does not decide what
to do about it. The Parser *reports* that it finished — it does not start the Engine.
The Kernal decides.

> **Why this matters (a real design principle):** if the Parser started the Engine
> directly, the Parser would need to *know about* the Engine. Now two data-plane
> components are coupled. Scale that up and you get a spaghetti graph where everything
> knows about everything. Keeping decisions in the control plane keeps the data-plane
> components ignorant of each other — they only know their queues and the Kernal.
> This is the same reason Kubernetes has a control plane, and why an OS kernel schedules
> processes instead of processes scheduling each other.

---

## 4. The lifecycle you already have — and the phase you're missing

Your `Subsystem` base class already encodes a state machine (read `subsystem.hpp`):

```
CREATED ──Init()──► INITIALIZED ──Start()──► STARTED ──Stop()──► STOPPING ──► STOPPED
```

And your `Kernal` drives all subsystems through it in lockstep: `InitAll()` moves
everyone to INITIALIZED, `StartAll()` moves everyone to STARTED.

The missing idea is that **not every subsystem should move through the machine at the
same time**. In your desired flow:

- On a **warm start**, the pipeline subsystems (DirReader/Lexer/Parser) might be
  initialized but **never started** — you skip straight to the Engine.
- On a **cold start**, the pipeline starts *first*, runs to completion, and only *then*
  does the Engine start.

So the Kernal needs a way to say "start *these* now, and *that one* later." Today it can
only say "start everyone." Part 3 is entirely about giving the Kernal that ability
without breaking the clean lifecycle you've built.

---

## 5. Init vs Start — a distinction you must respect

This trips up almost everyone coming from a garbage-collected language. Look again at
your `Subsystem`:

```cpp
Error Init()   // OnInit()  — validate, allocate, pre-flight checks. NO threads yet.
Error Start()  // OnStart() — create threads, begin doing work.
```

The split is deliberate and it is *exactly* the tool you need:

- **`Init` is cheap, safe, and side-effect-free-ish.** It checks preconditions. Your
  `DirectoryReader::OnInit()` verifies the `data/` directory exists. Your
  `Engine::OnInit()` verifies the Store pointer isn't null. Nothing *runs* yet.
- **`Start` is where the world begins to move.** Threads spawn. Queues start filling.

Why this helps you: you can **`Init` everything up front** (cheap validation, fail fast
if `data/` is missing or the disk is unreadable) but **`Start` selectively** based on the
boot decision. Init is your pre-flight; Start is takeoff. You don't want to discover a
broken runway *after* the plane is airborne.

> **Your turn (thinking exercise, no code yet):**
> Where should "does a saved index exist?" be *checked* — in `Store::OnInit()`, or in a
> separate method the Kernal calls between InitAll and StartAll? Consider: `OnInit` returns
> only an `Error`. Can it return *both* "everything's fine" *and* "here's whether an index
> exists"? What does that tell you about the return type you need? (Part 2 answers this.)

---

## 6. The poison pill, and why "done" is different from "stop"

Your pipeline already uses the **poison pill** pattern: a sentinel value (the empty
string `""`, or `ILP{"",""}`) pushed onto a queue to mean "no more real data is coming."
Read `DirectoryReader::Run()` — after scanning, it pushes `""`. Read
`Lexer::Run()` — when it pops `""`, it breaks.

But here is a subtlety you must see clearly, because it's central to question 3:

**In your current code, the Parser only ever receives its poison pill during shutdown.**

Trace it: `Lexer::OnStop()` (STEP 6) is what pushes `""` onto `parserQueue_`. Nothing
pushes a poison pill to the Parser at the *natural end of indexing* — only when someone
calls Stop. So right now, "Parser is done indexing" and "Parser is shutting down" are the
**same event**. For your desired flow they must become **two different events**:

```
  end-of-indexing  →  "I built the whole index"  →  Kernal starts Engine   (normal life)
  shutdown         →  "stop everything now"        →  threads join and exit  (teardown)
```

Question 3 is really: *how does the Parser tell the Kernal "I finished building the
index" — a normal-operation event — as opposed to "I'm being torn down"?* Part 4 covers
the full menu of C++ mechanisms for this (atomic flags, condition variables,
`std::promise`/`std::future`, callbacks). But the conceptual seed is here: **you need an
end-of-stream signal that propagates all the way through the pipeline during normal
running, not just at shutdown.**

---

## 7. A second subtlety: the Parser builds a throwaway tree

Read `Parser::Run()` carefully:

```cpp
while (running_.load(...)) {
    std::string token;
    parserQueue_.pop_blocking(token);
    BSTree tree;              // ← created INSIDE the loop, every iteration
    if (token.empty()) { tree.print(); break; }
    tree.insert(token);      // ← inserts into a tree that dies next iteration
}
```

The `BSTree` is constructed *inside* the loop, so each token goes into a brand-new tree
that is destroyed on the next iteration. Nothing is accumulated, and nothing is ever
written into the `Store`. Meanwhile `Engine::Search()` reads `store_->GetSearchIndex()`,
which is **never populated**. So today, every search returns "No results found."

I'm pointing this out not to fix it for you, but because your three questions can't
actually work until the Parser's output lands *in the Store*. Part 4 discusses the
handoff — how a data-plane worker safely publishes its result into shared state — and why
`std::thread::join()` gives you the memory-visibility guarantee that makes that handoff
safe. Keep this gap in mind; you'll close it yourself.

---

## 8. What "in-depth C++ understanding" means for this problem

Across this series you will meet these C++ ideas *in the context of your own code*:

- **Ownership & lifetime** — the Kernal owns subsystems (`unique_ptr`); `main` owns the
  queues and hands them out by reference. Who outlives whom decides what's safe.
  (Deep dive: `ownership-and-lifecycle-part1..4.md`.)
- **Value vs reference return** — how the Store hands its answer back without copying a
  huge index or dangling a pointer.
- **`std::optional`, `enum class`, and small result types** — expressing "found /
  not found / corrupted" without exceptions or magic numbers.
- **RAII for files** — `std::ifstream`/`std::ofstream` closing themselves; why you never
  write `close()` and never leak a handle even on an exception. (Deep dive: `cpp-file-io.md`.)
- **Threads, `join`, and happens-before** — how a background thread's writes become
  visible to the main thread, and why `join()` is a synchronization point.
- **Atomics & memory ordering** — `running_.load(acquire)` / `store(release)`; the
  minimum you need to reason about your poison-pill signalling. (Deep dive: `concurrency.md`.)
- **One-shot synchronization** — `std::promise`/`std::future` and
  `std::condition_variable` as clean ways to say "wake me when indexing is done."
- **Callbacks with `std::function`** — letting the Kernal inject "what to do when done"
  into the Parser without the Parser knowing what the Kernal is.

You do not need all of these for every question. Part 5 maps each mechanism to each of
your three questions so you can choose deliberately.

---

## 9. Before you move on

Make sure you can answer these in your own words (they're the load-bearing ideas):

1. Why is "boot" a *decision* and not just a *sequence* in your desired design?
2. What is the difference between a **warm start** and a **cold start** for your engine,
   concretely — which subsystems run in each?
3. Which plane (control or data) should answer "does an index exist?" and which should
   decide "what to do about it?" Why does keeping them separate matter?
4. Why are "Parser finished indexing" and "Parser is shutting down" two *different*
   events, and why does today's code conflate them?
5. Why does the `Init` / `Start` split give you exactly the lever you need for
   conditional startup?

When those feel solid, go to **Part 2** to design how the Store detects and loads a
saved index — and how it reports the answer up to the Kernal.
