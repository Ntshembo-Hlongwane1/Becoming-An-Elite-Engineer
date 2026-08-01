# `std::promise` and `std::future` — A Deep Lesson

> **Companion to** `startup-04-completion-signalling.md`. That doc was the *menu* — five ways
> to signal completion, with trade-offs. This doc goes all the way down on the mechanism I
> recommended there: **`std::promise` / `std::future`**. What they actually are, how they
> differ, what each difference *costs you*, and then a complete worked implementation for
> `Parser` → `Kernal`, followed by the poison-pill propagation fix that has to land first.
>
> **Header:** `<future>`. **Since:** C++11.
>
> **How to read this:** Part I is concept. Part II is the worked build. Part III is the
> prerequisite fix. If you're impatient, read §2 (the shared state) and §7 (the comparison
> table) — everything else is elaboration on those two.

---

# Table of contents

**Part I — The mechanism**
1. [The problem futures exist to solve](#1-the-problem-futures-exist-to-solve)
2. [The central mental model: one shared state, two handles](#2-the-central-mental-model-one-shared-state-two-handles)
3. [`std::promise` in depth — the write end](#3-stdpromise-in-depth--the-write-end)
4. [`std::future` in depth — the read end](#4-stdfuture-in-depth--the-read-end)
5. [The five failure modes, and what each one teaches](#5-the-five-failure-modes-and-what-each-one-teaches)
6. [Memory visibility: why publish-then-signal is safe](#6-memory-visibility-why-publish-then-signal-is-safe)
7. [**The deep comparison**](#7-the-deep-comparison)
8. [The real decisions (it's never "promise or future")](#8-the-real-decisions-its-never-promise-or-future)
9. [Siblings: `packaged_task`, `async`, `shared_future`](#9-siblings-packaged_task-async-shared_future)
10. [Cost model — and when *not* to use a future](#10-cost-model--and-when-not-to-use-a-future)

**Part II — Worked implementation: Parser → Kernal**
11. [Step 0 — Decide what crosses the boundary](#step-0--decide-what-crosses-the-boundary)
12. [Step 1 — Define the result type](#step-1--define-the-result-type)
13. [Step 2 — Give the Parser the write end](#step-2--give-the-parser-the-write-end)
14. [Step 3 — Fire it in `Run()`, in the right order](#step-3--fire-it-in-run-in-the-right-order)
15. [Step 4 — Guarantee it fires *exactly once, on every path*](#step-4--guarantee-it-fires-exactly-once-on-every-path)
16. [Step 5 — The Kernal holds the read end and waits](#step-5--the-kernal-holds-the-read-end-and-waits)
17. [Step 6 — Add a timeout with `wait_for`](#step-6--add-a-timeout-with-wait_for)
18. [Step 7 — Error propagation with `set_exception`](#step-7--error-propagation-with-set_exception)
19. [Step 8 — Wire it through `main.cpp`](#step-8--wire-it-through-maincpp)
20. [Step 9 — The two deadlocks you just armed](#step-9--the-two-deadlocks-you-just-armed)
21. [Design-decision → consequence table](#21-design-decision--consequence-table)

**Part III — Prerequisite: fix poison-pill propagation**
22. [Why the signal never arrives today](#22-why-the-signal-never-arrives-today)
23. [The three rules](#23-the-three-rules)
24. [Step-by-step fix](#24-step-by-step-fix)
25. [Verification checklist](#25-verification-checklist)
26. [Known limitation: the pill is not a real close](#26-known-limitation-the-pill-is-not-a-real-close)

---
---

# Part I — The mechanism

## 1. The problem futures exist to solve

Strip the vocabulary away. You have two threads. One of them will, at some unpredictable
future moment, *produce* something — a value, or just the fact that an event occurred, or an
error. The other thread needs that thing and cannot proceed without it.

```
  Thread A (Kernal, main)                 Thread B (Parser worker)
  ─────────────────────────               ────────────────────────
  ... start the pipeline                  popping tokens
  I need the index.                       building the index
  It doesn't exist yet.                   building...
  ┌──────────────────┐                    building...
  │ park me until    │  ◄──── ??? ────    receives poison pill
  │ it does          │                    the index is COMPLETE
  └──────────────────┘                    "here it is"
  wake, read index
  start the Engine
```

The question mark is the whole problem. Four things have to be true at once, and every
hand-rolled solution gets at least one wrong:

1. **Thread A must not burn CPU while waiting.** A `while (!done) {}` spin is a correctness-
   neutral, performance-catastrophic answer.
2. **Thread A must not miss the signal.** If B finishes *before* A starts waiting, A must
   still find out. (This is the lost-wakeup bug; a bare condition variable has it.)
3. **The data must be visible.** B wrote the index across dozens of cache lines. A must see
   *all* of it, not a torn half. This is a memory-model problem, not a signalling problem —
   but the two are entangled.
4. **Failure must travel too.** If B throws, A must not wait forever. "The producer died"
   has to be a deliverable outcome, not a hang.

`std::promise`/`std::future` is the standard library's answer to *exactly* this four-part
problem, for the case where it happens **once**.

> **If you know Go:** a `promise<void>` + `future<void>` is a `done := make(chan struct{})`
> where the producer does `close(done)` and the consumer does `<-done`. A `promise<T>` +
> `future<T>` is a **buffered channel of capacity 1 that is closed after one send**. That
> analogy is worth holding onto — but note the C++ version *also* carries exceptions, which
> Go channels do not.

---

## 2. The central mental model: one shared state, two handles

**This is the section that makes everything else obvious. If you only internalise one thing,
make it this.**

`std::promise` and `std::future` are **not two things that talk to each other.** They are
**two handles onto one invisible third object**, allocated on the heap, which the standard
calls the **shared state**.

```
                    ┌───────────────────────────────────────┐
                    │        THE SHARED STATE               │
                    │        (heap allocated)               │
                    │                                       │
   std::promise<T> ─┼──►  • storage for T (or exception_ptr)│◄─┬─ std::future<T>
   "the write end"  │     • ready flag                      │  │  "the read end"
                    │     • mutex + condition variable      │  │
                    │     • atomic reference count          │  │
                    └───────────────────────────────────────┘  │
                              ▲                                │
                              └── both handles point here ─────┘
```

The shared state is created when you construct the `promise`. The `future` is *carved off*
it by `promise::get_future()`. Neither handle owns the state outright — it's reference
counted, and it dies when the last handle dies.

Everything that seems arbitrary about the API falls out of this one picture:

| Observation | Why, given the model |
|---|---|
| `promise` is move-only, never copyable | Two promises pointing at one state could both `set_value()`. The type system forbids it so the race can't exist. |
| `future` is move-only too | Same reason applied to reads: `get()` **moves** the value out. Two futures both moving out of one slot is a double-move. |
| `get_future()` works exactly once | It is *transferring* the read handle, not manufacturing one. A second call has nothing left to give → throws `future_already_retrieved`. |
| `get()` works exactly once | It moves `T` out of the slot. After that the slot is empty and the future is `valid() == false`. |
| Signalling before waiting is safe | The state has a **ready flag**, which is durable. `set_value()` sets it. A later `wait()` sees it's already set and returns immediately. Nothing is "missed". |
| Destroying the promise unblocks the future | The state can tell (via refcount) that its writer is gone. It stores a `broken_promise` exception and becomes ready. The waiter wakes with an error instead of hanging. |

That last row is the one that separates futures from a hand-rolled mutex+CV+bool. Go back to
your `RingBuffer` and look at `not_empty.wait(...)`. If the pushing thread dies, that
`wait` blocks **forever**. Nothing in the CV knows a producer existed. The shared state
knows, because it is refcounted, and it converts producer death into a *delivered error*.

> **Say it in one line:** the promise is the write end, the future is the read end, and the
> pipe between them is a one-shot, refcounted, exception-carrying, memory-fencing slot.

---

## 3. `std::promise` in depth — the write end

### 3.1 Construction and ownership

```cpp
std::promise<Result> p;                       // allocates the shared state
std::future<Result>  f = p.get_future();      // carve off the read handle — ONCE

std::promise<Result> p2 = std::move(p);       // OK — transfer ownership
// std::promise<Result> p3 = p2;              // COMPILE ERROR — copy is deleted
```

Move-only is the load-bearing design decision. It means "who is allowed to fulfil this
promise?" always has exactly one answer at any instant, and that answer is enforced at
compile time. When you move a promise into a thread's lambda, you are transferring a
*responsibility*, and the compiler makes the transfer visible.

The moved-from promise is left **stateless** — `p` above no longer refers to anything.
Calling `p.set_value()` after the move throws `future_error(no_state)`. Not UB, but a hard
error; treat a moved-from promise as radioactive.

### 3.2 The four ways to fulfil it

```cpp
p.set_value(result);                 // 1. deliver a value
p.set_value();                       //    (for promise<void> — signal only)
p.set_exception(std::current_exception());   // 2. deliver a failure
p.set_value_at_thread_exit(result);          // 3. deliver, but stay unready until TLS teardown
p.set_exception_at_thread_exit(ptr);         // 4. same, for failures
```

**Exactly one** of these may succeed, ever. A second call throws
`future_error(promise_already_satisfied)`. The shared state is a one-shot slot; once it holds
a value or an exception, it is sealed.

The `_at_thread_exit` variants are the subtle pair, and they exist for one specific bug: the
waiter wakes up, sees the result, and tears down resources that the producing thread's
`thread_local` destructors are still using. The normal `set_value` makes the future ready
*immediately*, while the producer thread is still running its exit sequence. The
`_at_thread_exit` form stores the value now but withholds readiness until that thread's
thread-locals have been destroyed. You will almost certainly not need it — but knowing *why*
it exists tells you something real: **"the future is ready" and "the producer thread is
finished" are different events.** Don't conflate them. (You'll meet this again in §20.)

### 3.3 `promise<void>` vs `promise<T>` vs `promise<T&>`

Three specialisations, three meanings:

- **`promise<void>`** — pure signal. "It happened." No payload. `set_value()` takes no
  argument; `future<void>::get()` returns nothing but still rethrows a stored exception.
  This is the done-channel.
- **`promise<T>`** — signal **plus** a value, moved across the thread boundary. This is a
  buffered channel of one.
- **`promise<T&>`** — signal plus a *reference*. Rare and dangerous: you are now responsible
  for guaranteeing the referent outlives the waiter's read. Skip it unless you have a
  specific reason.

> **The upgrade path matters practically.** Starting with `promise<void>` and later needing
> statistics is a mechanical change — swap the type, pass the value at `set_value`, read it
> at `get()`. There is no restructuring. So "start with `void`" is not a decision you can be
> punished for later, which is why it's a good default.

### 3.4 The destructor — the most important method

```cpp
~promise();
```

If the promise is destroyed **without** having been fulfilled, and a future is still waiting
on the shared state, the destructor stores a `std::future_error` with code
`std::future_errc::broken_promise` and makes the state ready.

Read that again, because it is not a footnote — it is the safety net that makes the whole
mechanism robust. It means:

> **A waiting thread cannot be stranded by a producer that dies, returns early, throws, or
> simply forgets.** The worst case is a *delivered error*, never a hang.

Compare with your `RingBuffer`: if `Lexer::Run` crashed before pushing its pill, the Parser
would sit in `pop_blocking` until the heat death of the universe. The promise cannot do that
to you. In Part II §15 we lean on this deliberately.

---

## 4. `std::future` in depth — the read end

### 4.1 The API surface

```cpp
T    get();                                   // block, then MOVE the value out (one-shot)
void wait() const;                            // block until ready; do NOT consume
std::future_status wait_for(duration) const;  // block up to a duration
std::future_status wait_until(time_point) const;
bool valid() const noexcept;                  // do I still refer to a shared state?
std::shared_future<T> share() noexcept;       // convert to a copyable, multi-read handle
```

### 4.2 `get()` vs `wait()` — a real distinction, not a synonym

This trips people up, so be precise:

| | `wait()` | `get()` |
|---|---|---|
| Blocks until ready | yes | yes |
| Consumes the state | **no** | **yes** |
| Returns the value | no | yes (by move) |
| Rethrows a stored exception | **no** | **yes** |
| `valid()` afterwards | still `true` | now `false` |
| Callable repeatedly | yes | no |

The exception row is the one that bites. `wait()` returning tells you the state is *ready* —
and "ready" includes "ready with an exception." If you only `wait()`, a failure looks
identical to a success and you will sail on with a broken index.

**Rule: use `get()` when you need to know whether it succeeded.** Which, for a boot sequence,
is always. `wait()` is for when you genuinely only care about timing (e.g. you want to
observe readiness without consuming, then `get()` elsewhere).

For `future<void>`, `get()` returns `void` — but *still rethrows*. So even with no payload,
prefer `get()` over `wait()`:

```cpp
try {
    indexReady.get();          // returns void, but throws if the Parser failed
} catch (const std::exception& e) {
    // boot failure, handled
}
```

### 4.3 `wait_for` and `std::future_status`

```cpp
switch (f.wait_for(std::chrono::seconds(30))) {
    case std::future_status::ready:    /* value or exception is available */ break;
    case std::future_status::timeout:  /* not done yet — state unchanged  */ break;
    case std::future_status::deferred: /* only from std::async(deferred)  */ break;
}
```

Two things to internalise:

- **`timeout` does not cancel anything.** The producer keeps running. You've only stopped
  *waiting*. C++ futures have no cancellation — there is no `f.cancel()`. If you want the
  producer to stop, you need your own mechanism (you already have one: `running_`).
- **`deferred` can only appear from `std::async(std::launch::deferred, ...)`.** A future from
  a `promise` will never return it. Handle the case or assert it away.

### 4.4 `valid()` and the moved-from trap

`valid()` is `false` in three situations: default-constructed, moved-from, or already
`get()`-ed. Calling `get()` or `wait()` on an invalid future is **undefined behaviour** by
the standard (most implementations throw `future_error(no_state)`, but don't rely on it).

Since the Kernal will store the future as a member and may be re-entered, guard it:

```cpp
if (indexReady_.valid()) {
    indexReady_.get();
}
```

### 4.5 The destructor — and the `std::async` exception

```cpp
~future();
```

For a future obtained from a **`promise`** or a **`packaged_task`**, the destructor is
non-blocking: it releases its reference to the shared state and returns. Instantly.

For a future returned by **`std::async`** with the default or `launch::async` policy, the
destructor **blocks until the task completes.** This is the single most notorious wart in
`<future>`, because it makes this innocent-looking line synchronous:

```cpp
std::async(std::launch::async, heavyWork);   // temporary future destroyed at ';'
                                             // → blocks right here. Not async at all.
```

You are using `promise` directly, so you are not exposed — but you must know the rule,
because the moment someone "simplifies" your code to `std::async` the behaviour changes
underneath them. (§9 covers when `async` is actually the right call.)

---

## 5. The five failure modes, and what each one teaches

Every one of these is a `std::future_error` carrying a `std::future_errc` code. Learning them
is learning the invariants of the shared state.

| # | Error | Triggered by | The invariant it protects |
|---|---|---|---|
| 1 | `broken_promise` | promise destroyed unfulfilled | *A waiter is never stranded.* Producer death is an outcome, not a hang. |
| 2 | `future_already_retrieved` | `get_future()` called twice | *One read handle per state.* Reads are not to be duplicated by accident (use `share()` if you mean it). |
| 3 | `promise_already_satisfied` | `set_value`/`set_exception` twice | *The slot is one-shot.* No last-writer-wins ambiguity. |
| 4 | `no_state` | any operation on a moved-from or default-constructed handle | *A handle either refers to a state or is inert.* |
| 5 | (UB, often `no_state`) | `get()` twice on a `future` | *The value was moved out.* Use `shared_future` if multiple reads are intended. |

Note the pattern: **four of the five are compile-time-impossible-to-express or immediately
throwing.** The library aggressively converts "concurrency bug you'd debug for two days" into
"exception at the exact line." That is the argument for using it over mutex + CV + bool,
where all five of these become silent hangs or silent corruption.

**#1 deserves special emphasis for your use case.** In Part II you will *deliberately* let
`broken_promise` fire on the abort path, because "the Parser was stopped before it finished
indexing" is genuinely an error the Kernal should hear about, and getting it for free — with
no code — is better than remembering to signal it.

---

## 6. Memory visibility: why publish-then-signal is safe

This is the part that separates "I can use futures" from "I understand futures."

The Parser builds an index: hundreds of heap allocations, pointer writes, node links. Those
writes land in the Parser core's store buffer and its L1. The Kernal runs on a **different
core**, with a different L1. Nothing in C++ or in your hardware automatically guarantees that
core 2 sees core 1's writes, or sees them *in the order they were made*.

So why is this correct?

```cpp
// Parser thread
store_->AddSearchIndex(token, doc);   // (A) hundreds of writes
promise_.set_value(result);           // (B) the release

// Kernal thread
indexReady_.get();                    // (C) the acquire
store_->Lookup("kernel");             // (D) reads what (A) wrote — guaranteed correct
```

The standard ([futures.state]) guarantees that a call which successfully **sets** the result
*synchronizes-with* a call which successfully **detects the ready state** produced by that
setting. Concretely: **(B) synchronizes-with (C).**

And `synchronizes-with` is the primitive that builds `happens-before`:

- (A) is *sequenced-before* (B) — same thread, program order.
- (B) *synchronizes-with* (C) — the library's guarantee.
- (C) is *sequenced-before* (D) — same thread, program order.
- Therefore **(A) happens-before (D)**, transitively.

"Happens-before" is precisely the guarantee "(D) sees everything (A) wrote." The
implementation delivers this with a release fence on `set_value` and an acquire fence on
`get`/`wait` — the exact same release/acquire pairing you already hand-wrote in
`ringbuffer.hpp` (`store(..., memory_order_release)` / `load(..., memory_order_acquire)`).
You've built this machine; the future just packages it.

**The operational rule, and the only one you need to remember:**

> ### Publish first. Signal second. Always.
>
> Every write the waiter needs to see must be *sequenced before* `set_value()` on the
> producer's thread. Anything you write **after** `set_value()` is a data race — the waiter is
> already awake and reading.

Concretely, in `Parser::Run`, this ordering is a bug:

```cpp
promise_.set_value();               // ✗ Kernal wakes NOW
store_->Publish(std::move(tree));   // ✗ ...and races this
```

and this is correct:

```cpp
store_->Publish(std::move(tree));   // ✓ all writes land first
promise_.set_value();               // ✓ then the release makes them visible
```

Same two lines. Opposite correctness. This is why I keep saying the *ordering* is the lesson,
not the API.

---

## 7. The deep comparison

You asked for a deep comparison of the two, so here it is at full width. But read §8
immediately after — because the framing "which one do I use?" is itself the misconception.

### 7.1 Head-to-head

| Axis | `std::promise<T>` | `std::future<T>` |
|---|---|---|
| **Role** | Write end / producer handle | Read end / consumer handle |
| **Direction of data** | Pushes *in* | Pulls *out* |
| **Who holds it** | The thread doing the work (Parser) | The thread that needs the result (Kernal) |
| **Created by** | Direct construction (`std::promise<T> p;`) | `p.get_future()`, or returned by `async`/`packaged_task` |
| **Allocates the shared state** | **Yes** — construction creates it | No — attaches to an existing one |
| **Copyable** | No (deleted) | No (deleted) |
| **Movable** | Yes | Yes |
| **Blocks** | **Never.** `set_value` is non-blocking | **Yes.** `get`/`wait`/`wait_for` block |
| **One-shot operation** | `set_value`/`set_exception` — once | `get()` — once (`wait()` is repeatable) |
| **How many per state** | Exactly one | Exactly one (or many, via `shared_future`) |
| **Destructor if unused** | Stores `broken_promise`, wakes the waiter | Releases the reference; **non-blocking** (except `async` futures) |
| **Carries exceptions** | Sends them (`set_exception`) | Rethrows them (`get()`) |
| **Memory-model role** | The **release** — publishes writes | The **acquire** — makes them visible |
| **Failure if misused** | `promise_already_satisfied`, `no_state` | `future_already_retrieved`, `no_state`, UB on double-`get` |
| **Go analogue** | `ch <- v` then `close(ch)` | `v := <-ch` |
| **Can you cancel it?** | No | No — `wait_for` timing out cancels nothing |

### 7.2 The asymmetries that actually matter

Four of those rows are not trivia. They're the ones that change your design:

**(a) Only the promise allocates.**
Construction of the `promise` is the allocation point. That's why you construct it in the
Kernal (or in `main`) and *move* it into the Parser — the lifetime of the shared state
outlives the Parser thread, which is exactly what you want, because the Kernal must still be
able to `get()` after the Parser is gone.

**(b) Only the future blocks.**
The Parser is never delayed by signalling. `set_value` writes the slot, flips the ready flag,
notifies the CV, returns. Your data-plane thread pays essentially nothing to report
completion. All the waiting cost lands on the control-plane thread, which had nothing better
to do. **This asymmetry is the reason futures are the right shape for a boot sequence** —
the expensive operation (blocking) is on the thread you *want* to block.

**(c) Only the future has an exception-consuming operation.**
Errors flow strictly producer → consumer. There is no back-channel. The Kernal cannot tell
the Parser anything through this pipe. If you need bidirectional control ("stop indexing"),
that is a *separate* mechanism — you already have `running_`. Don't try to make one future do
both jobs.

**(d) The destructors are asymmetric, and that asymmetry is the safety net.**
`~promise` *communicates* (broken_promise). `~future` *is silent*. So an abandoned producer
is loud and an abandoned consumer is quiet. That's the right default: nobody waiting is not a
bug, but somebody waiting on nobody is.

### 7.3 The diagram to keep in your head

```
     PARSER THREAD                                    KERNAL / MAIN THREAD
     ─────────────                                    ────────────────────

  promise<Result> p                                   future<Result> f
        │                                                    │
        │  (both refer to the same shared state)             │
        ▼                                                    ▼
  ┌─────────────────────────────────────────────────────────────────┐
  │  SHARED STATE                                                   │
  │  ┌───────────────┬──────────┬──────────────┬─────────────────┐  │
  │  │ value or      │  ready   │  mutex +     │  atomic         │  │
  │  │ exception_ptr │  flag    │  condvar     │  refcount = 2   │  │
  │  └───────────────┴──────────┴──────────────┴─────────────────┘  │
  └─────────────────────────────────────────────────────────────────┘

  t0  building index ...                              f.get()  → refcount sees not-ready
                                                               → sleeps on the condvar
                                                                 (0% CPU)
  t1  store_->Publish(...)   ← writes A                         still sleeping
  t2  p.set_value(r)         ← RELEASE fence
        • writes r into the slot
        • ready = true
        • notify                             ─────►    wakes, ACQUIRE fence
                                                       sees ready
                                                       MOVES r out
                                                       sees all of A ✓
  t3  thread returns, ~p runs                          uses the index
      (state already sealed → no broken_promise)       starts the Engine
      refcount 2 → 1
  t4                                                   ~f → refcount 1 → 0
                                                       shared state freed
```

---

## 8. The real decisions (it's never "promise or future")

Here's the reframe that the comparison table earns you.

**You never choose between them.** They are two ends of one object. Choosing a `promise` over
a `future` is like choosing the sending end of a telephone call over the receiving end. If
you have one, you have both.

The genuine decisions are these four, and *these* are what you should be deliberating:

**Decision 1 — `promise<void>` or `promise<T>`?**
Do you need a payload? If the Kernal only needs "indexing finished," `void` is honest and
minimal. If it wants token counts, document counts, and elapsed time for a boot log, use a
small struct. My steer for you: **use a struct**, because you're building a search engine and
"how many tokens did the cold boot index?" is a number you will want on day one, and it costs
nothing extra.

**Decision 2 — `future` or `shared_future`?**
`future` if exactly one place waits. `shared_future` if several do (say, both the Kernal *and*
a metrics subsystem). Start with `future`; `f.share()` upgrades it in one line whenever you
need it.

**Decision 3 — raw `promise`, or `packaged_task`, or `async`?**
Covered in §9. Short version: your Parser is a *long-lived subsystem thread with a lifecycle*,
not a task that runs and returns, so **raw `promise` is correct**.

**Decision 4 — is a one-shot the right shape at all?**
This is the most important one, and the one people skip. A promise fires **once**. If the
Kernal will eventually need to hear "indexing done," *and* "a subsystem crashed," *and*
"reindex finished," *and* "backpressure is high" — you don't want four futures. You want the
**event queue** from `startup-04` §4 Mechanism 5, built on the `RingBuffer` you already have.

> **My recommendation stands from `startup-04`:** build the promise/future version now. It's
> the smallest correct thing, it teaches you the ordering discipline in §6, and the discipline
> transfers *completely* to the event-queue version later. Do not skip to the event queue
> before you've felt why publish-before-signal matters.

---

## 9. Siblings: `packaged_task`, `async`, `shared_future`

Three things in `<future>` that also produce futures. Knowing when each replaces a raw
promise is most of the practical skill.

### 9.1 `std::packaged_task<Sig>` — "wrap a callable, get its future"

```cpp
std::packaged_task<Result()> task([]{ return buildIndex(); });
std::future<Result> f = task.get_future();
std::thread t(std::move(task));      // running the task fulfils the promise automatically
```

It is a `promise` with the fulfilment wired to a function's return. Return normally →
`set_value`. Throw → `set_exception`. You cannot forget to fulfil it.

**Use when:** the unit of work is genuinely "call this function, get its return value."
**Don't use for your Parser**, because `Parser::Run` is not a function whose *return value* is
the index — it's a loop with a lifecycle, a `running_` flag, a stop path, and a poison pill.
The completion event and the thread's return are different moments (remember §3.2). Forcing
it into a `packaged_task` would fight your `Subsystem` design.

### 9.2 `std::async` — "run this and give me a future"

```cpp
std::future<Result> f = std::async(std::launch::async, buildIndex);
```

The convenience layer: it creates the task, picks a thread, and hands you the future.

**Use when:** fire-and-collect work, especially several parallel pieces you'll gather.
**Don't use for your Parser** for three reasons:
1. You lose the `std::thread` handle, so you cannot `join()` it on your terms — but your
   `Subsystem::OnStop` contract is *built* on deterministic joins.
2. The destructor-blocks behaviour (§4.5) means a stray temporary silently serialises you.
3. With the default policy `launch::async | launch::deferred`, the implementation may choose
   *not to start a thread at all* and run lazily inside `get()`. For a subsystem that must
   actually be running after `Start()` returns, that is disqualifying.

### 9.3 `std::shared_future<T>` — the multicast read end

```cpp
std::shared_future<Result> sf = f.share();   // f becomes invalid
std::shared_future<Result> sf2 = sf;         // copies are fine
Result a = sf.get();                         // get() is repeatable...
Result b = sf2.get();                        // ...and returns const& — no move-out
```

`shared_future::get()` returns a `const T&` rather than moving, which is precisely why it can
be called many times from many threads.

**Use when:** more than one waiter. A classic: several subsystems that must not start until
the index exists — hand each a copy of the same `shared_future`, and they all unblock on one
`set_value`. That is a **broadcast barrier**, and it's a genuinely useful pattern to keep in
your pocket for when the Engine isn't the only thing gated on indexing.

### 9.4 Which to reach for

| Situation | Reach for |
|---|---|
| Long-lived subsystem thread with its own lifecycle (**your Parser**) | **raw `std::promise`** |
| "Run this function, give me the result" | `std::packaged_task` |
| Same, but you don't want to manage the thread | `std::async(std::launch::async, ...)` |
| Many threads must wait on one event | `std::shared_future` |
| Many *different kinds* of event, repeatedly | **not a future** — event queue on `RingBuffer` |

---

## 10. Cost model — and when *not* to use a future

Futures are not free, and knowing the price tells you where they belong.

A `promise`/`future` pair costs, roughly:

- **One heap allocation** for the shared state (~50–100 bytes: storage for `T`, an
  `exception_ptr`, a ready flag, a mutex, a condition variable, a refcount).
- **An atomic refcount** incremented/decremented on each handle copy/move/destroy.
- **A mutex acquisition** inside `set_value` and inside `wait`.
- **A futex/condvar syscall** on the actual block and the actual wake (this is the big one —
  microseconds, not nanoseconds).

So: **hundreds of nanoseconds to a few microseconds per event.**

Now put that next to your workload. Your `parserQueue_` will carry *millions* of tokens
during a cold boot of a 100k-file corpus. A future per token would be a catastrophe — that's
why the token path is a lock-free-ish `RingBuffer` and not a future.

But the completion event happens **once per boot**. Two microseconds, once. It is
free in every sense that matters.

> ### The rule that generalises
>
> **Futures are for control-plane events (rare, meaningful, one-shot). Ring buffers are for
> data-plane throughput (constant, high-volume, streaming).**
>
> You already built the data-plane tool. This is the control-plane tool. A system needs both,
> and using either one for the other's job is the mistake.

**Don't use a future when:**
- The event repeats → CV, or a queue.
- The event is per-item in a hot loop → ring buffer.
- Many observers need many events → event queue / pub-sub.
- You need to *cancel* → futures have no cancellation; use your `running_` flag.
- The producer and consumer are the same thread → you don't have a concurrency problem.

---
---

# Part II — Worked implementation: Parser → Kernal

> **Read Part III first if you're implementing.** The signal is meaningless until the poison
> pill reaches the Parser during *normal* completion, which today it does not. Part II assumes
> Part III has landed. I've ordered it this way because you asked for the promise/future
> material first — but build in the other order.

The goal: when `Parser::Run` drains its queue, the Kernal — parked on the main thread — wakes
up, learns whether indexing succeeded, and proceeds to start the Engine.

---

## Step 0 — Decide what crosses the boundary

Before any code: **what, exactly, is the Kernal receiving?** Three options, and this is
Decision 1 from §8:

| Option | Signature | What it buys you |
|---|---|---|
| A | `promise<void>` | "Done." Minimum viable. |
| B | `promise<size_t>` | "Done, 8412 tokens." |
| C | `promise<IndexResult>` | "Done, 8412 tokens, 1204 docs, 3.2s." |

**Pick C.** You are building a search engine; the boot log wants those numbers, and the
struct gives you somewhere to add fields without touching a single signature later. The extra
cost over A is one struct definition.

> **Note what is *not* crossing the boundary: the index itself.** The index goes into the
> `Store` (shared state both sides already reach), and the future carries only the
> *notification plus metadata*. Trying to move the whole index through the future would work,
> but it conflates "here is a signal" with "here is the data," and the `Store` already exists
> to be the data's home. **Signal through the future; publish through the Store.** That
> separation is what §6's ordering rule operates on.

---

## Step 1 — Define the result type

Create `internal/kernal/core/headerfiles/indexresult.hpp`:

```cpp
#pragma once
#include <cstddef>
#include <chrono>

// What the Parser reports to the Kernal when a cold-boot indexing pass completes.
// Deliberately a plain value type: it is MOVED across a thread boundary through
// std::promise/std::future, so it must be cheap to move and own nothing shared.
struct IndexResult {
    std::size_t tokensIndexed  = 0;
    std::size_t documentsSeen  = 0;
    std::chrono::milliseconds elapsed{0};
};
```

Two properties this type must have, and why:

- **Cheaply movable, owning nothing shared.** It travels between threads. If it held a raw
  pointer into Parser-owned memory, the Kernal would read it after the Parser thread is gone.
- **Default-constructible with sane values.** You'll want to construct it before the loop and
  fill it in as you go.

---

## Step 2 — Give the Parser the write end

The design question: **how does the promise get into the Parser?** Two options.

### Option A — constructor injection (matches your existing style)

```cpp
Parser(RingBuffer<std::string, 1024>& parserQueue, std::promise<IndexResult> donePromise)
    : parserQueue_(parserQueue), donePromise_(std::move(donePromise)) {}
```

### Option B — a setter after construction

```cpp
void SetCompletionPromise(std::promise<IndexResult> p) { donePromise_ = std::move(p); }
```

**Choose A.** Your whole codebase already injects dependencies at construction — look at
`DirectoryReader(dirQueue)`, `Lexer(dirQueue, lineQueue, parserQueue)`, `Engine(store)`. More
importantly, A makes the promise a **construction-time invariant**: a `Parser` that exists is
a `Parser` that can report completion. With B, there's a window where the object exists but
can't signal, and `OnStart` has to defend against it.

Note the parameter is **by value**, then `std::move`d. That is the idiomatic way to accept a
move-only type: the caller writes `std::move(p)` at the call site, making the transfer of
responsibility visible in their code — exactly the point from §3.1.

### `parser.hpp`

```cpp
// internal/parser/parser.hpp
#pragma once
#include "internal/kernal/core/headerfiles/subsystem.hpp"
#include "internal/kernal/core/headerfiles/indexresult.hpp"
#include "internal/kernal/core/datastructures/ringbuffer.hpp"
#include <thread>
#include <future>

class Parser : public Subsystem {
public:
    Parser(RingBuffer<std::string, 1024>& parserQueue,
           std::promise<IndexResult> donePromise)
        : parserQueue_(parserQueue)
        , donePromise_(std::move(donePromise)) {}

    std::string Name() override;

protected:
    Error OnInit() override;
    Error OnStart() override;
    Error OnStop() override;

private:
    RingBuffer<std::string, 1024>& parserQueue_;
    std::thread run_thread_;

    // The WRITE end of the completion channel. Owned by the Parser; fulfilled
    // exactly once, from Run(), on the Parser's own thread.
    std::promise<IndexResult> donePromise_;

    void Run();
};
```

> **Ownership check.** The `Parser` owns the promise; the `Kernal` owns the future. The shared
> state is refcounted, so **it survives the Parser's destruction** — the Kernal can still
> `get()` afterwards. That is not an accident of the implementation; it's the reason
> `get_future()` hands you an independent handle instead of a pointer back into the promise.

---

## Step 3 — Fire it in `Run()`, in the right order

Here is the current `Run()` with its problems marked, then the fix.

```cpp
void Parser::Run() {
    int tokensProcessed = 0;
    while (running_.load(std::memory_order_acquire)) {
        std::string token;
        parserQueue_.pop_blocking(token);

        BSTree tree;                    // ✗ BUG: constructed INSIDE the loop.
                                        //   Every token gets a fresh 1-node tree,
                                        //   destroyed at the closing brace.
        if (token.empty()) {
            tree.print();               // ✗ prints an empty tree, always
            break;
        }
        tree.insert(token);
    }
    Log(Name(), "Done. Tokens: " + std::to_string(tokensProcessed));
                                        // ✗ tokensProcessed is never incremented
}
```

Fix the tree lifetime first — **there is no point signalling "the index is ready" when the
index is destroyed once per iteration.** This is §6's publish step, and it has to be real.

```cpp
void Parser::Run() {
    Log(Name(), "Processing started");

    const auto start = std::chrono::steady_clock::now();
    IndexResult result;
    BSTree tree;                                    // ✓ ONE tree, outside the loop

    while (running_.load(std::memory_order_acquire)) {
        std::string token;
        parserQueue_.pop_blocking(token);

        if (token.empty()) {                        // poison pill = end of stream
            Log(Name(), "Received end signal");
            break;
        }

        tree.insert(token);
        result.tokensIndexed++;                     // ✓ actually counted
    }

    result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    // ─────────────────────────────────────────────────────────────────
    //  §6 ORDERING. Both lines matter and the order is not negotiable.
    // ─────────────────────────────────────────────────────────────────
    store_->PublishIndex(std::move(tree));   // (1) PUBLISH — all writes land
    donePromise_.set_value(result);          // (2) SIGNAL  — release fence

    Log(Name(), "Done. Tokens: " + std::to_string(result.tokensIndexed));
}
```

> **Do not write these two lines in the other order.** With `set_value` first, the Kernal is
> awake and reading the Store while `PublishIndex` is still writing it. That is a data race:
> undefined behaviour, and in practice a crash or a half-populated index that appears once a
> week on a fast machine. Re-read §6 until this feels obvious rather than memorised.

> **`store_` doesn't exist on the Parser yet.** The Parser takes no `Store*` today. Adding it
> is the same constructor-injection pattern as `Engine(Store* store)`, and there's a deeper
> gap behind it: `parserQueue_` is `RingBuffer<std::string>`, so it carries a bare token with
> **no document association** — and an inverted index maps token → *documents*. The Lexer has
> the filepath in `ILP` but throws it away at `lexer.cpp:152`. Fixing that (a `TokenDoc {
> std::string token; std::string doc; }` on the parser queue) is its own piece of work,
> flagged in `startup-04` §3. I'm leaving it as-is here so the promise/future lesson stays
> legible, but **the publish step is not actually complete until that's done.**

---

## Step 4 — Guarantee it fires *exactly once, on every path*

Step 3's `Run()` has a latent bug. Trace the shutdown path:

```
Ctrl+C → StopAll → Parser::OnStop
   running_ = false
   push_blocking("")           ← unblocks Run
Run: while condition is now FALSE → loop never re-enters → falls out
   → reaches set_value  ...but with a HALF-BUILT index, reported as success ✗
```

And a second path: if `tree.insert()` throws (`bad_alloc` on a large corpus is not exotic),
the exception propagates out of `Run()`, `std::terminate` is called because it escaped a
thread function, and the process dies — never reaching `set_value` at all.

You need a policy. There are three, and the choice is a real design decision:

| Policy | Behaviour on abort | Verdict |
|---|---|---|
| **P1** Always `set_value` | Kernal is told "success" with a partial index | ✗ Actively wrong. Lies to the Kernal. |
| **P2** `set_exception` on abort | Kernal gets a typed error it can react to | ✓ Explicit and clear |
| **P3** Let `broken_promise` fire | Promise destructs unfulfilled → Kernal gets `broken_promise` | ✓ Free, and correct by default |

**P3 is the quiet winner, and it's why §3.4 matters.** If `Run` exits without fulfilling, the
`Parser`'s destructor destroys `donePromise_`, and the Kernal's `get()` throws
`future_error(broken_promise)`. You get "the Parser did not complete" **with zero code**, on
*every* abnormal path including ones you didn't think of.

Use **P3 as the safety net** and **P2 for failures you can name**:

```cpp
void Parser::Run() {
    const auto start = std::chrono::steady_clock::now();
    IndexResult result;
    BSTree tree;

    try {
        bool drained = false;

        while (running_.load(std::memory_order_acquire)) {
            std::string token;
            parserQueue_.pop_blocking(token);

            if (token.empty()) {
                drained = true;              // ✓ we ended because the STREAM ended
                break;                       //   — not because we were told to stop
            }

            tree.insert(token);
            result.tokensIndexed++;
        }

        if (!drained) {
            // We exited via running_ == false: an abort, not a completion.
            // Leave the promise UNFULFILLED. ~promise delivers broken_promise. (P3)
            Log(Name(), "Stopped before end of stream; indexing incomplete");
            return;
        }

        result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);

        store_->PublishIndex(std::move(tree));   // PUBLISH
        donePromise_.set_value(result);          // then SIGNAL

    } catch (...) {
        // Anything thrown while indexing becomes a delivered failure, not a
        // std::terminate. set_exception is the exception's border crossing. (P2)
        donePromise_.set_exception(std::current_exception());
    }
}
```

Three things to notice, because each is a general lesson:

1. **`drained` distinguishes *why* the loop ended.** "The stream ended" and "I was told to
   stop" are different outcomes and must not collapse into one signal. Your `Lexer::Run` has
   the same ambiguity today (`lexer.cpp:79-87`) — the `break` on a pill and the loop-condition
   exit are indistinguishable to any caller.
2. **The `catch(...)` is mandatory, not defensive style.** An exception escaping a thread
   function is `std::terminate` — no stack unwind, no destructors, no message. `set_exception`
   converts that into an exception the Kernal rethrows on `get()`, **on the Kernal's thread**,
   inside its own `try`. That is a genuinely remarkable capability: futures are the only
   standard mechanism that moves an exception across a thread boundary.
3. **P3 is load-bearing, so don't "tidy" it away.** A future reader will see the `return` with
   no `set_value` and want to "fix" it. Leave the comment; it explains that the omission *is*
   the mechanism.

---

## Step 5 — The Kernal holds the read end and waits

### `kernal.hpp` additions

```cpp
#include "internal/kernal/core/headerfiles/indexresult.hpp"
#include <future>

class Kernal {
public:
    // ...
    // Called by main() at wiring time, with the future carved from the promise
    // that was handed to the Parser.
    void SetIndexFuture(std::future<IndexResult> f) { indexReady_ = std::move(f); }

private:
    // The READ end. Non-blocking to destroy; safe to outlive the Parser.
    std::future<IndexResult> indexReady_;

    Error AwaitIndexing_();
};
```

### `kernal.cpp` — the wait

```cpp
Error Kernal::AwaitIndexing_() {
    if (!indexReady_.valid()) {
        return Error("No indexing future was wired; cannot await completion");
    }

    Log(GetName(), "Waiting for indexing to complete...");

    try {
        // get() — NOT wait(). get() rethrows a stored exception; wait() would
        // return happily on a failed index and we would start the Engine on
        // garbage. See §4.2.
        const IndexResult r = indexReady_.get();

        Log(GetName(), "Indexing complete: "
                     + std::to_string(r.tokensIndexed) + " tokens, "
                     + std::to_string(r.documentsSeen) + " docs, "
                     + std::to_string(r.elapsed.count()) + " ms");
        return Error("");

    } catch (const std::future_error& e) {
        // broken_promise lands here: the Parser died or aborted without finishing.
        return Error(std::string("Indexing did not complete: ") + e.what());
    } catch (const std::exception& e) {
        // Anything the Parser passed via set_exception.
        return Error(std::string("Indexing failed: ") + e.what());
    }
}
```

### Slotting it into `StartAll`

```cpp
Error Kernal::StartAll() {
    Log(GetName(), "Starting all subsystems...");

    Store* store = static_cast<Store*>(GetSubsystem(SubsystemId::Store));
    StartSubSystem_(SubsystemId::Store);

    if (store->HasSearchIndex()) {
        Log(GetName(), "INDEX AVAILABLE - warm start");
    } else {
        Log(GetName(), "No index - cold start, building...");

        StartSubSystem_(SubsystemId::DirReader);
        StartSubSystem_(SubsystemId::Lexer);
        StartSubSystem_(SubsystemId::Parser);

        Error err = AwaitIndexing_();      // ◄── the main thread parks here
        if (HasError(err)) {
            return err;                    // boot fails; StopAll unwinds
        }
    }

    StartSubSystem_(SubsystemId::Engine);   // only now can the Engine serve
    return Error("");
}
```

**Which thread is where, at the moment of the wait:**

| Thread | State |
|---|---|
| main | parked inside `indexReady_.get()` — 0% CPU, no spinning |
| DirReader::Run | scanning `C:/data`, pushing paths |
| Lexer::Run | reading files, pushing lines |
| Lexer::Worker ×2 | tokenising, pushing tokens |
| Parser::Run | consuming tokens, building the tree |

The pipeline runs at full speed. The control plane sleeps. That's the shape you want — and
it's asymmetry (b) from §7.2 paying off exactly as advertised.

---

## Step 6 — Add a timeout with `wait_for`

Blocking forever on a cold boot is a bad operational property. A hung `DirectoryReader` on a
network path would leave your process alive, silent, and useless.

```cpp
Error Kernal::AwaitIndexing_() {
    if (!indexReady_.valid()) {
        return Error("No indexing future was wired");
    }

    constexpr auto kTimeout   = std::chrono::minutes(10);
    constexpr auto kHeartbeat = std::chrono::seconds(5);

    const auto deadline = std::chrono::steady_clock::now() + kTimeout;

    while (true) {
        const auto status = indexReady_.wait_for(kHeartbeat);

        if (status == std::future_status::ready) {
            break;                                     // fall through to get()
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return Error("Indexing timed out after 10 minutes");
        }
        Log(GetName(), "Still indexing...");           // liveness for the operator
    }

    try {
        const IndexResult r = indexReady_.get();       // ready → returns at once
        Log(GetName(), "Indexing complete: " + std::to_string(r.tokensIndexed) + " tokens");
        return Error("");
    } catch (const std::exception& e) {
        return Error(std::string("Indexing failed: ") + e.what());
    }
}
```

**The critical thing to understand about the timeout path** (§4.3): returning an `Error` here
does **not** stop the Parser. It is still running. It still holds the promise. Nothing was
cancelled — you only stopped *waiting*. So the caller must then run `StopAll()`, which sets
`running_ = false` and joins. If you return the error and let `main` fall through to
`return 1` without stopping, you'll hit the `Kernal` destructor with live threads. (Your
destructor does handle that — `kernal.cpp:8-16` — which is good defensive design, but it
logs a warning for a reason: it means someone skipped the orderly path.)

> **Notice the loop-with-heartbeat shape.** It's not just for logging. Consuming the timeout
> in small slices lets the control plane stay responsive — you could also check `g_shutdown`
> in that loop so Ctrl+C works *during* a cold boot. Right now it wouldn't: `main` doesn't
> install the signal handler until after `StartAll()` returns (`main.cpp:61-69`), so a cold
> boot is uninterruptible. **Move `std::signal` above `StartAll`.** Small fix, real bug.

---

## Step 7 — Error propagation with `set_exception`

This deserves its own step because it's the capability that has no equivalent anywhere else
in the standard library.

Define a domain exception:

```cpp
// internal/kernal/core/headerfiles/indexerror.hpp
#pragma once
#include <stdexcept>
#include <string>

class IndexingError : public std::runtime_error {
public:
    explicit IndexingError(const std::string& what) : std::runtime_error(what) {}
};
```

Throw it inside the Parser's `try` block:

```cpp
if (result.tokensIndexed == 0) {
    throw IndexingError("Corpus produced zero tokens - check the stop-word filter");
}
```

The `catch (...)` in `Run()` captures it into an `exception_ptr` via
`std::current_exception()` and stores it with `set_exception`. Then on the Kernal's thread:

```cpp
try {
    const IndexResult r = indexReady_.get();
    // ...
} catch (const IndexingError& e) {          // ◄── your domain error, rethrown HERE
    return Error(std::string("Indexing failed: ") + e.what());
} catch (const std::future_error& e) {      // broken_promise
    return Error(std::string("Parser did not complete: ") + e.what());
} catch (const std::exception& e) {         // anything else
    return Error(std::string("Unexpected indexing error: ") + e.what());
}
```

**Sit with what just happened.** An exception was thrown on the Parser thread, its stack was
unwound, the exception object was captured into a refcounted `std::exception_ptr`, carried
across a thread boundary through the shared state, and **rethrown on the main thread**, where
a `catch` clause matched its *original dynamic type*. `IndexingError` — not a copy, not a
string, the actual type.

There is no other standard mechanism that does this. Not condition variables, not atomics, not
your `RingBuffer`. If the *only* thing futures gave you were cross-thread exceptions, they'd
still be worth the heap allocation.

> **That zero-token check is not hypothetical for you.** `lexer.cpp:151` reads
> `if (!token.empty() && StopWords::isStopWord(token))` — it pushes a token to the parser
> queue **only when it IS a stop word**. The condition is inverted; you are currently indexing
> exclusively "the", "and", "of". The predicate should be `!StopWords::isStopWord(token)`.

---

## Step 8 — Wire it through `main.cpp`

The wiring is where the two ends are created and separated. Order matters.

```cpp
int main() {
    Log("SonarSearch", "Starting...");

    Kernal kernal;

    RingBuffer<std::string, 1024> dirQueue;
    RingBuffer<ILP, 1024>         lineQueue;
    RingBuffer<std::string, 1024> parserQueue;

    // ===== The completion channel =====
    // STEP 1: create the promise. THIS allocates the shared state (§7.2a).
    std::promise<IndexResult> indexPromise;

    // STEP 2: carve off the future BEFORE the promise is moved away.
    //         After std::move(indexPromise), calling get_future() on it throws
    //         future_error(no_state). This ordering is not stylistic — it is
    //         the only order that compiles into working code.
    std::future<IndexResult> indexFuture = indexPromise.get_future();

    kernal.Register(SubsystemId::Store,     new Store());
    kernal.Register(SubsystemId::DirReader, new DirectoryReader(dirQueue));
    kernal.Register(SubsystemId::Lexer,     new Lexer(dirQueue, lineQueue, parserQueue));

    // STEP 3: MOVE the write end into the Parser. Ownership transfer, visible
    //         at the call site because the type is move-only (§3.1).
    kernal.Register(SubsystemId::Parser,
                    new Parser(parserQueue, std::move(indexPromise)));

    Store* store = dynamic_cast<Store*>(kernal.GetSubsystem(SubsystemId::Store));
    kernal.Register(SubsystemId::Engine, new Engine(store));

    // STEP 4: MOVE the read end into the Kernal.
    kernal.SetIndexFuture(std::move(indexFuture));

    // Install signal handlers BEFORE StartAll, so Ctrl+C works during a long
    // cold boot (see Step 6).
    std::signal(SIGINT,  HandleShutdownSignal);
    std::signal(SIGTERM, HandleShutdownSignal);

    if (Error e = kernal.InitAll(); !e.GetMessage().empty()) {
        std::cerr << "Init failed: " << e.GetMessage() << std::endl;
        return 1;
    }

    if (Error e = kernal.StartAll(); !e.GetMessage().empty()) {
        std::cerr << "Start failed: " << e.GetMessage() << std::endl;
        kernal.StopAll();               // ← unwind; do not fall through to ~Kernal
        return 1;
    }

    Log("SonarSearch", "Running. Press Ctrl+C to exit...");
    while (!g_shutdown) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (Error e = kernal.StopAll(); !e.GetMessage().empty()) {
        std::cerr << "Shutdown errors:\n" << e.GetMessage() << std::endl;
        return 1;
    }

    Log("SonarSearch", "Stopped cleanly.");
    return 0;
}
```

**The one line people get wrong is STEP 2.** Once `indexPromise` is moved into the `Parser`,
it is a hollow shell. `get_future()` on it throws `no_state`. The future must be extracted
while the promise is still yours. Get the sequence — *construct, carve, move* — into muscle
memory.

**Lifetime audit, because this is where it could go wrong:**
- `indexPromise` is moved into a heap-allocated `Parser` owned by `kernal.subsystems_`.
- `indexFuture` is moved into `kernal.indexReady_`.
- Both handles reference the same refcounted shared state → **it lives as long as the last
  handle**, which is the Kernal. Even if the Parser were destroyed first, `indexReady_.get()`
  is safe: it would find the sealed value, or `broken_promise` if unfulfilled.
- The local `indexPromise`/`indexFuture` objects go out of scope at the end of `main`, but
  they are moved-from husks by then. Destroying them does nothing.

---

## Step 9 — The two deadlocks you just armed

Introducing a blocking wait into a lifecycle manager creates two hazards. Both are real; both
have been shipped by people who knew better.

### Deadlock 1 — the self-join

If the Parser ever calls back into the Kernal *on its own thread*, and the Kernal responds by
stopping the Parser:

```
Parser::Run  (parser thread)
  └─ kernal_->OnParserDone()
       └─ Kernal calls Parser::Stop()
            └─ OnStop() → run_thread_.join()
                 └─ joining the thread that is currently executing this call
                      → std::system_error(resource_deadlock_would_occur) → terminate
```

**The promise/future design is immune to this**, and it's worth being explicit about *why*:
`set_value` is data, not control. The Parser never executes a line of Kernal code. The Kernal
wakes on **its own thread** and calls `Parser::Stop()` from there, so the join is always
cross-thread. This is the structural argument for futures over callbacks (`startup-04` §4
Mechanism 4) — a callback would run the Kernal's reaction *on the Parser's thread* and walk
straight into this.

### Deadlock 2 — waiting on a producer you already stopped

```
main:  StartAll → AwaitIndexing_()  → blocks in get()
Ctrl+C arrives...
       but main is INSIDE StartAll and never reaches the g_shutdown loop
```

If the Parser then aborts without fulfilling, §3.4 rescues you: `~promise` fires
`broken_promise` and `get()` returns with an error. But if the Parser is *alive and stuck*
(blocked in `pop_blocking` because the Lexer stalled), nothing fulfils the promise and nothing
destroys it. `get()` blocks forever.

**This is exactly why Step 6's timeout is not optional.** Bounded waits in a boot sequence are
a hard rule, not a refinement. An unbounded `get()` in `StartAll` is a hang waiting for a slow
disk.

> **General principle worth carrying beyond this project:** never block indefinitely on the
> control plane. Every wait in a lifecycle manager should have a deadline and a defined
> action on expiry. The data plane can block forever — that's what `pop_blocking` is for. The
> control plane must always be able to make progress toward shutdown.

---

## 21. Design-decision → consequence table

The summary of Part II. Each row is a fork in the road and what taking each branch actually
costs you.

| Decision | Option chosen | What it affects | What the other branch would cost |
|---|---|---|---|
| Payload type | `promise<IndexResult>` | Kernal can log real boot metrics | `promise<void>`: no numbers; but a trivial upgrade later |
| Injection | constructor | Parser can *always* signal — a class invariant | setter: a window where the object can't report; `OnStart` must defend |
| Who allocates | `main` constructs the promise | Shared state outlives the Parser | Parser-constructed: Kernal has no way to obtain the future |
| Read call | `get()` not `wait()` | Failures surface as exceptions | `wait()`: a failed index looks like success → Engine serves garbage |
| Ordering in `Run` | publish **then** signal | Kernal sees a complete index | reversed: data race, UB, intermittent corruption |
| Abort policy | leave unfulfilled (P3) | `broken_promise` for free on every abnormal path | always-`set_value`: Kernal told "success" with a partial index |
| Exception policy | `catch(...)` + `set_exception` | Errors cross the thread boundary with their type | no catch: `std::terminate`, no unwind, no message |
| Wait bound | `wait_for` + deadline | Boot can fail loudly instead of hanging | bare `get()`: a stalled Lexer hangs the process silently |
| Tree lifetime | one `BSTree` outside the loop | An index actually exists to publish | per-iteration: nothing is ever built; the signal is meaningless |
| Loop-exit reason | `drained` flag | "stream ended" ≠ "was stopped" | conflated: a Ctrl+C mid-boot reports success |
| Signal handler | installed **before** `StartAll` | Ctrl+C works during a cold boot | after: an 8-minute cold boot is uninterruptible |

---
---

# Part III — Prerequisite: fix poison-pill propagation

> **Build this first.** Everything in Part II hangs off the assumption that the Parser's
> poison pill means *"the corpus is fully indexed."* Today it means *"we are shutting down."*
> Wire the promise to the current pill and the Kernal will be told "indexing complete" at the
> exact moment the user is killing the process.

## 22. Why the signal never arrives today

Trace the natural end of a corpus scan through the real code:

```
1. DirectoryReader::Run       finishes the scan
                              pushes ""  → dirQueue          [directoryreader.cpp:66]  ✓

2. Lexer::Run                 pops "" → logs → break         [lexer.cpp:84-87]
                              ✗ forwards NOTHING downstream
                              Run() returns. Thread ends.

3. Lexer::Worker ×2           still blocked in lineQueue_.pop_blocking()
                              ✗ nobody pushes ILP{"",""} — that only happens
                                in OnStop  [lexer.cpp:56-57]

4. Parser::Run                still blocked in parserQueue_.pop_blocking()
                              ✗ nobody pushes "" — that only happens
                                in OnStop  [lexer.cpp:68]
```

**The end-of-stream signal dies at step 2.** The corpus is fully read, the pipeline has done
all its real work, and then three threads park forever while `main` spins its 100 ms sleep
loop waiting for a Ctrl+C that has no reason to come.

The root cause is a category error worth naming precisely:

> **End-of-stream is being treated as a shutdown concern.** It isn't. Draining is a *normal
> operating event* — it happens on a healthy, successful run. Shutdown is an *exceptional*
> event. Today both are implemented in `OnStop()`, so the normal path can only be reached
> through the exceptional one.

---

## 23. The three rules

Everything below follows from three rules. Learn the rules; the code is mechanical.

### Rule 1 — N consumers require N pills

A poison pill is **consumed** by whichever thread pops it. It does not broadcast. With two
`Lexer::Worker` threads on `lineQueue`, one pill unblocks *one* worker; the other stays parked
forever.

Your `OnStop` already gets this right (`lexer.cpp:56-57` pushes two) — the fix is to apply the
same rule on the normal path.

> **Corollary:** the moment you make the worker count configurable, a hardcoded `2` becomes a
> hang. Introduce `kWorkerCount` and derive the pill count from it. Never write the number
> twice.

### Rule 2 — Each stage forwards its end-marker on the way out, whatever the reason

The current design puts downstream pills in `OnStop`, which means downstream only learns about
the end during shutdown. Invert it:

> **Every stage pushes its downstream pill(s) on the exit path of its own loop — regardless of
> *why* it is exiting.**

The payoff is large and structural:
- **Normal drain** propagates all the way down without anyone calling `Stop()`.
- **Abort** propagates too — a stage that exits early still tells downstream it's finished, so
  downstream unwinds instead of hanging.
- **`OnStop` gets much simpler**: set the flag, unblock the *input* queue, join. It no longer
  touches downstream queues at all.

There's a name for this: **each stage owns the lifecycle of the queue it produces into.** Only
the producer may close it, and it always closes it exactly once, on the way out.

### Rule 3 — The last consumer out forwards the marker

`lineQueue` has two consumers feeding one `parserQueue`. If both workers pushed a pill to
`parserQueue`, the Parser (one consumer) would get two — one consumed, one left as garbage in
the buffer.

So: workers decrement a shared atomic counter as they exit, and **whichever one observes that
it is the last** forwards the single pill downstream.

```cpp
if (activeWorkers_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    // fetch_sub returns the value BEFORE subtracting.
    // Seeing 1 means "I took it from 1 to 0" — I am the last worker out.
    parserQueue_.push_blocking(std::string(""));
}
```

The `acq_rel` ordering is deliberate and not decoration: the **release** half publishes
everything this worker wrote (all its `parserQueue_` pushes) before decrementing; the
**acquire** half means the last worker observes the other worker's writes before it forwards.
That is §6's publish-before-signal rule again, one layer down — the same discipline, applied
between peers instead of between stages.

---

## 24. Step-by-step fix

### Step 1 — Make the worker count a named constant

`lexer.hpp`:

```cpp
private:
    static constexpr int kWorkerCount = 2;

    std::vector<std::thread> workers_;                 // replaces worker1_, worker2_
    std::atomic<int> activeWorkers_{0};                // Rule 3's counter
    std::atomic<bool> downstreamClosed_{false};        // Step 5's idempotency guard
```

A vector rather than two named members, because Rule 1's "N pills for N consumers" should be
derived from one number, not duplicated at three call sites.

### Step 2 — `Lexer::Run` forwards worker pills on its exit path

```cpp
void Lexer::Run() {
    Log(Name(), "Coordinator started");
    int filesProcessed = 0;

    while (running_.load(std::memory_order_acquire)) {
        std::string file;
        dirQueue_.pop_blocking(file);

        if (file.empty()) {                       // end of stream from DirReader
            Log(Name(), "Coordinator received end signal");
            break;
        }

        FILE* fp = fopen(file.c_str(), "r");
        if (!fp) {
            std::cerr << "\n [" << Name() << "] Failed to open: " << file << std::endl;
            continue;
        }

        char line[456];
        while (fgets(line, sizeof(line), fp) != nullptr) {
            if (!running_.load(std::memory_order_acquire)) break;

            size_t len = strlen(line);
            if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';

            lineQueue_.push_blocking({file, std::string(line)});
        }

        if (ferror(fp)) {
            std::cerr << "\n [" << Name() << "] Read error on: " << file << std::endl;
        }
        fclose(fp);
        filesProcessed++;
    }

    // ─────────────────────────────────────────────────────────────
    //  RULE 2: forward the end-marker on the way out, whatever the
    //  reason. RULE 1: one pill PER worker.
    //  This runs on BOTH the drain path and the abort path.
    // ─────────────────────────────────────────────────────────────
    for (int i = 0; i < kWorkerCount; ++i) {
        lineQueue_.push_blocking(ILP{"", ""});
    }

    Log(Name(), "Coordinator done. Files: " + std::to_string(filesProcessed));
}
```

The only change is the loop before the final log — but it moves pill emission from the
shutdown path to the *always* path, which is the whole fix in miniature.

### Step 3 — Workers forward the parser pill, last-one-out

```cpp
void Lexer::Worker(std::string id) {
    Log(Name(), "Worker " + id + " started");
    int tokensProcessed = 0;

    while (running_.load(std::memory_order_acquire)) {
        ILP line;
        lineQueue_.pop_blocking(line);

        if (line.filepath.empty() && line.line.empty()) {   // pill
            Log(Name(), "Worker " + id + " received end signal");
            break;
        }

        auto tokens = splitLine(line.line);
        for (auto& token : tokens) {
            token.erase(
                std::remove_if(token.begin(), token.end(),
                    [](unsigned char c) { return std::isspace(c) || std::ispunct(c); }),
                token.end());

            // NOTE: the original condition was `StopWords::isStopWord(token)`,
            // which forwards ONLY stop words. Negated here — see Part II Step 7.
            if (!token.empty() && !StopWords::isStopWord(token)) {
                parserQueue_.push_blocking(token);
                tokensProcessed++;
            }
        }
    }

    Log(Name(), "Worker " + id + " done. Tokens: " + std::to_string(tokensProcessed));

    // ─────────────────────────────────────────────────────────────
    //  RULE 3: the LAST worker out closes the downstream queue.
    //  acq_rel: release publishes this worker's pushes before the
    //  decrement; acquire lets the last worker observe the others'.
    // ─────────────────────────────────────────────────────────────
    if (activeWorkers_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        CloseParserQueue_();
    }
}
```

> **Why the pill push must come *after* the log and after the loop:** the pill is the promise
> "I have pushed everything I will ever push." Any `parserQueue_.push_blocking` that happens
> after the pill is a token arriving behind the end-of-stream marker — the Parser has already
> broken out and will never see it. Same discipline as §6: **publish everything, then signal.**

### Step 4 — `OnStart` initialises the counter *before* spawning

```cpp
Error Lexer::OnStart() {
    running_.store(true, std::memory_order_release);
    downstreamClosed_.store(false, std::memory_order_release);
    activeWorkers_.store(kWorkerCount, std::memory_order_release);   // BEFORE spawning

    try {
        workers_.reserve(kWorkerCount);
        for (int i = 0; i < kWorkerCount; ++i) {
            workers_.emplace_back(&Lexer::Worker, this, std::to_string(i + 1));
        }
        run_thread_ = std::thread(&Lexer::Run, this);

    } catch (const std::system_error& e) {
        running_.store(false, std::memory_order_release);

        // Some workers may have started. Feed pills to whatever exists so the
        // joins below can complete.
        for (size_t i = 0; i < workers_.size(); ++i) {
            lineQueue_.push_blocking(ILP{"", ""});
        }
        for (auto& w : workers_) if (w.joinable()) w.join();
        if (run_thread_.joinable()) run_thread_.join();

        return Error("Thread creation failed: " + std::string(e.what()));
    }

    return Error("");
}
```

> **The `store` before spawning is not cosmetic.** If you set the counter after
> `emplace_back`, a fast worker could hit an empty queue, exit, and decrement a counter that
> is still `0` — sending it negative and firing the "last worker" branch on the *first* worker
> out. Initialise shared state before you create the threads that read it. General rule.

### Step 5 — Make closing the downstream queue idempotent

Now that both the normal path and the abort path can reach the close, and `OnStop` might race
with a worker that is closing concurrently, the close must be exactly-once:

```cpp
void Lexer::CloseParserQueue_() {
    bool expected = false;
    // compare_exchange_strong: only the thread that flips false→true proceeds.
    // Every other caller sees `expected` updated to true and does nothing.
    if (downstreamClosed_.compare_exchange_strong(expected, true,
                                                  std::memory_order_acq_rel)) {
        parserQueue_.push_blocking(std::string(""));
        Log(Name(), "Downstream (Parser) queue closed");
    }
}
```

This is the standard **exactly-once guard**, and it's worth recognising as a pattern. The same
shape works for one-time initialisation, one-time cleanup, and one-time close. Note it is
doing the same job for the pill that the shared state's ready-flag does for `set_value` — both
turn "this must happen once" into an atomic state transition.

### Step 6 — `OnStop` gets dramatically simpler

```cpp
Error Lexer::OnStop() {
    // 1. Tell every thread to stop at its next loop check.
    running_.store(false, std::memory_order_release);

    // 2. Unblock the coordinator if it is parked on its INPUT queue.
    //    (Only ever touch your own input here — downstream is Run's job now.)
    dirQueue_.push_blocking(std::string(""));

    // 3. Join the coordinator. On its way out it pushes kWorkerCount pills
    //    to lineQueue (Step 2), which unblocks the workers for us.
    if (run_thread_.joinable()) {
        run_thread_.join();
    }

    // 4. Join the workers. The last one out closes parserQueue (Step 3).
    for (auto& w : workers_) {
        if (w.joinable()) w.join();
    }
    workers_.clear();

    // 5. Belt and braces: if the workers never ran (e.g. Start failed), make
    //    sure the Parser is not left waiting. Idempotent, so it is safe even
    //    when a worker already closed it.
    CloseParserQueue_();

    Log(Name(), "All threads joined");
    return Error("");
}
```

Compare with the original (`lexer.cpp:43-72`): the downstream pills are gone from `OnStop`
entirely. It now does only what a stop should do — **signal, unblock input, join**. The
end-of-stream propagation lives with the threads that own it. That's Rule 2 paying off in
readability, not just correctness.

### Step 7 — The Parser's `OnStop` also stops touching its own queue

```cpp
Error Parser::OnStop() {
    running_.store(false, std::memory_order_release);

    // Unblock Run() if it is parked on pop_blocking. This is the Parser's own
    // INPUT queue, and pushing here is legitimate for an abort — but note the
    // pill may ALREADY be in flight from the Lexer's last worker. Run() breaks
    // on the first one it sees; a spare left in the buffer is harmless because
    // nothing reads the queue after this point.
    parserQueue_.push_blocking(std::string(""));

    if (run_thread_.joinable()) {
        run_thread_.join();
    }
    return Error("");
}
```

Mostly unchanged — but now understand *why* it's still correct. On the normal path, `Run` has
already exited and fulfilled the promise; this extra push writes one string into a ring buffer
nobody will ever read. Wasteful, not wrong. If that offends you, gate it on
`run_thread_.joinable() && !finished_` — but the simple version is defensible and I'd leave
it.

### The full flow after the fix

```
DirReader::Run  scan done
                push "" → dirQueue                                    [1 pill]
                          │
Lexer::Run      pop "" → break
                push ILP{"",""} ×2 → lineQueue                        [2 pills, Rule 1]
                          │
Lexer::Worker1  pop pill → break → fetch_sub: 2→1, not last, exits silently
Lexer::Worker2  pop pill → break → fetch_sub: 1→0, LAST  ─┐           [Rule 3]
                                                          │
                                     push "" → parserQueue│           [1 pill]
                          ┌───────────────────────────────┘
Parser::Run     pop "" → drained = true → break
                store_->PublishIndex(tree)     ← PUBLISH              [§6]
                donePromise_.set_value(result) ← SIGNAL
                          │
Kernal (main)   indexReady_.get() returns ────┘
                logs the metrics
                StartSubSystem_(Engine)
```

End-to-end, with no one calling `Stop()`. That is the property you were missing.

---

## 25. Verification checklist

Work through these deliberately — each targets a specific way the fix can be subtly wrong.

**Normal completion**
- [ ] Cold boot on a small corpus (10 files) reaches `Kernal: Indexing complete` **without**
      Ctrl+C.
- [ ] Both worker "done" lines appear in the log; exactly one "Downstream (Parser) queue
      closed".
- [ ] `Parser: Done. Tokens: N` shows a **non-zero** N (if it's 0, the stop-word predicate is
      still inverted — Part II Step 7).
- [ ] `Kernal` logs the token count *after* the Parser logs it — proving the ordering held.

**Worker-count invariance (Rule 1)**
- [ ] Set `kWorkerCount = 4`, rerun. Still terminates. If it hangs, a pill count is hardcoded
      somewhere.
- [ ] Set `kWorkerCount = 1`. Still terminates.

**Abort paths**
- [ ] Ctrl+C *during* a large cold boot exits within a second or two, no hang.
- [ ] On abort, the Kernal reports `broken_promise` (§3.4 firing as designed) rather than
      claiming success.
- [ ] Ctrl+C *before* any file is read (empty corpus dir) still exits cleanly.

**Idempotency (Step 5)**
- [ ] Run under a debugger with a breakpoint in `CloseParserQueue_`. It is entered at most
      once per boot, even though it's called from two places.

**Warm start**
- [ ] With a persisted index present, `StartAll` takes the `HasSearchIndex()` branch, never
      touches the future, and `AwaitIndexing_` is not called. Confirm `indexReady_` is left
      `valid()` and its destruction is silent (§4.5).

**Under a race detector**
- [ ] `g++ -fsanitize=thread` (or ASan on MSVC) over a full cold boot. Zero reports. If you
      see one on the `BSTree`, you likely put `set_value` before `PublishIndex` — §6.

---

## 26. Known limitation: the pill is not a real close

Be honest about what this design still can't do, because it's the seed of the next lesson.

**The abort deadlock.** `Lexer::Run` can be blocked inside
`lineQueue_.push_blocking(...)` because the queue is full. If both workers have already
exited (say, an abort raced them), nobody will ever drain it, `not_full` never fires, and
`Run` blocks forever — so `OnStop`'s `join()` blocks forever, and shutdown hangs.

With 1024 slots and a fast consumer this is unlikely, but "unlikely" on a 100k-file corpus
means "eventually, on the user's machine, not yours."

**Why it exists:** a poison pill is an *in-band* signal. It travels through the same channel
as the data, so it is subject to the same backpressure as the data. A full queue blocks the
pill exactly as it blocks a token.

**The real fix** is an *out-of-band* close on the queue itself:

```cpp
template<typename T, size_t SIZE>
class RingBuffer {
public:
    void close() {
        { std::lock_guard<std::mutex> lk(mtx); closed_ = true; }
        not_empty.notify_all();      // wake every consumer
        not_full.notify_all();       // wake every blocked producer
    }

    // pop_blocking returns false when the queue is closed AND drained.
    bool pop_blocking(T& item);
    // push_blocking returns false if the queue was closed while waiting.
    bool push_blocking(const T& item);
};
```

That single change dissolves most of Part III: no pill counting (Rule 1 evaporates — `close()`
wakes *all* consumers), no last-one-out coordination (Rule 3 evaporates), no idempotency guard
(the flag is already the guard), and no deadlock, because `close()` also wakes blocked
producers. Consumers just loop `while (queue.pop_blocking(item))` and exit naturally when it
returns false.

> **Why I'm still telling you to build the pill version first.** Poison pills are how you
> learn that *end-of-stream is a piece of data with a lifetime and an owner*. Once Rules 1–3
> are in your hands and you've felt where they chafe, `close()` stops being an API you copied
> and becomes the obvious consolidation of three rules you derived yourself. Build it, feel
> the friction, then replace it. That order is the lesson.
>
> It is also, not coincidentally, the same lesson as §2: `close()` is to a queue what the
> shared state's **ready flag** is to a future — durable, broadcast, unmissable, and immune to
> the lost-wakeup bug that a bare notification has. You'll have built the same idea twice, at
> two different scales, which is usually the sign you've found something real.

---

## Appendix — the whole mechanism on one page

```cpp
#include <future>

// ── SETUP (one thread, before the work starts) ──────────────────────
std::promise<T> p;                       // allocates the shared state
std::future<T>  f = p.get_future();      // carve the read end — ONCE, before moving p
                                         // then std::move(p) to the producer

// ── PRODUCER ────────────────────────────────────────────────────────
publishEverythingTheConsumerWillRead();  // (1) PUBLISH  — must come first
p.set_value(result);                     // (2) SIGNAL   — release fence
// or:  p.set_exception(std::current_exception());
// or:  return without fulfilling → ~promise → broken_promise, consumer unblocked

// ── CONSUMER ────────────────────────────────────────────────────────
try {
    if (f.wait_for(timeout) != std::future_status::ready) { /* bounded! */ }
    T result = f.get();                  // acquire fence; moves out; rethrows
} catch (const std::exception& e) {
    // producer's exception, rethrown here with its original type
}
```

**Five things to remember:**
1. One shared state, two handles. Everything else follows.
2. Publish, **then** signal. Never the reverse.
3. `get()`, not `wait()` — only `get()` surfaces failures.
4. Unfulfilled promises deliver `broken_promise`. That's a feature; use it.
5. Bound every control-plane wait.
```
