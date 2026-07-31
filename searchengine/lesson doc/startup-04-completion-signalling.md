# Startup & Boot Architecture — Part 4: Completion Signalling

> **Answers your Question 3:** *"When the Parser receives its poison pill, it must alert the
> Kernal that indexing is complete, so the Kernal can start the Engine and accept queries."*
>
> This is the deepest concurrency doc in the series. The problem is a classic:
> **one thread (the Parser) must tell another thread (the Kernal, on the main thread) that a
> one-time event has happened — and hand over the result safely.** C++ gives you several
> mechanisms; this doc is the menu, the trade-offs, and the memory-safety rules. Read
> `concurrency.md` alongside it.

---

## 1. Frame the problem precisely

Two threads are involved:

```
  Main thread (Kernal::Boot)                  Parser thread (Parser::Run)
  ──────────────────────────                  ───────────────────────────
  start pipeline                              popping tokens off parserQueue_
  ...need to WAIT here...          ◄──────    building the index
  ...until Parser says "done"...   signal!    receives poison pill ("")
  start Engine                                → the stream has ended
```

Three things must happen, in this order, correctly:

1. **The Parser detects end-of-stream** (it pops the poison pill).
2. **The Parser publishes its result** — the built index must become visible to the Store /
   Engine (memory visibility, §7).
3. **The Parser notifies the Kernal**, which is *blocked waiting*, so it can proceed.

Getting (3) without (2) gives you a race: the Kernal starts the Engine, the Engine reads a
half-written index. Order matters. Publish, *then* signal.

> **The event is one-shot.** "Indexing complete" happens exactly once per cold boot. That's
> important — it steers you toward mechanisms designed for single-fire events
> (`std::promise`/`future`) rather than repeatable ones.

---

## 2. First, fix the poison-pill plumbing (a prerequisite)

Recall from Part 1 §6: today the Parser's poison pill only arrives during **shutdown**
(`Lexer::OnStop()` pushes it). For Question 3 you need it to arrive at the **natural end of
indexing** — when the DirectoryReader has run out of files and the Lexer has drained.

Trace the end-of-stream all the way through:

```
DirReader finishes scan → pushes ""  onto dirQueue
Lexer coordinator pops "" → breaks → Run() returns
   BUT the two Lexer workers are still blocked on lineQueue (nobody sent THEM a pill)
   AND parserQueue never gets a pill during normal running
```

So the end-of-stream signal currently **dies at the Lexer coordinator**. For a normal-run
completion signal, the pill must propagate: DirReader → Lexer coordinator → Lexer workers →
Parser. Each stage, on seeing "no more input," must forward an appropriate pill downstream
*before it exits* — not only in `OnStop`.

> **Your turn (design, then implement):** where should the Lexer forward pills to its
> workers and to the Parser *during normal completion* (not shutdown)? You have two workers,
> so how many pills must reach `lineQueue` so *both* workers unblock and exit? And how many
> must reach `parserQueue`? (Careful: if you have N consumers on a queue, you need N pills —
> each consumer eats exactly one and stops. This "one pill per consumer" rule is why
> `Lexer::OnStop` pushes *two* line-queue pills today.)

This is the unglamorous prerequisite. Question 3's signalling is pointless if the Parser
never actually reaches "done" during normal operation.

---

## 3. The publish step: get the index into the Store

Part 1 §7 flagged that the Parser builds a throwaway local `BSTree` and never writes to the
Store. Before the Parser can meaningfully signal "the index is ready," the index has to
*exist somewhere the Engine can read it* — the Store's `searchIndex_`.

Design questions for you (this is data-plane design, not signalling yet):

- The Parser receives *tokens* on `parserQueue_`. But `searchIndex_` maps *token →
  documents*. Where does the document name for each token come from? (Look back: the Lexer
  had the filepath in `ILP`, but it only forwards the bare token string to `parserQueue_`.
  So either the pill/packet carries the doc name, or the Lexer must be changed to send
  token+doc. This is a real gap — the current `parserQueue_` type (`std::string`) can't
  carry the document association an inverted index needs.)
- Once the Parser has (token, doc), it calls `store_->AddSearchIndex(token, doc)`. But the
  Parser doesn't currently hold a `Store*`. How does it get one? (Same pattern as the Engine:
  pass `Store*` into the Parser's constructor, like `Engine(Store* store)`.)

> I'm deliberately leaving these open. They're the interesting design work. The point of
> flagging them: **"signal that indexing is done" is meaningless unless indexing actually
> produced something in shared state.** Sequence your work: (a) make the Parser write to the
> Store, (b) make end-of-stream reach the Parser, (c) *then* add the completion signal below.

---

## 4. The menu of signalling mechanisms

Now the heart of Question 3. You have five idiomatic options, from crudest to cleanest for
*this specific* one-shot handoff.

### Mechanism 1 — `std::atomic<bool>` flag + polling (crude)

The Parser sets a flag; the Kernal spins checking it.

```cpp
std::atomic<bool> indexingDone{false};   // shared
// Parser, after finishing:   indexingDone.store(true, std::memory_order_release);
// Kernal:  while (!indexingDone.load(std::memory_order_acquire)) { /* spin or sleep */ }
```

- **Pro:** dead simple; you already use atomics (`running_`).
- **Con:** **busy-waiting**. The Kernal burns CPU spinning, or you sprinkle `sleep()` which
  is a guess. This is the pattern you use for *"should I keep running?"* checks (fine,
  because you check it in a loop that's doing other work), but it's poor for *"block until
  one event."* You wouldn't `while(!done) sleep(10ms)` in production. Know it; don't pick it.

### Mechanism 2 — `std::condition_variable` (the general tool)

You already *have* condition variables — read your `RingBuffer`'s `not_empty`/`not_full`.
A CV lets a thread **sleep until notified**, no spinning.

```cpp
std::mutex m;
std::condition_variable cv;
bool done = false;                    // the "predicate" the CV guards

// Parser (producer of the event):
{ std::lock_guard<std::mutex> lk(m); done = true; }
cv.notify_one();

// Kernal (waiter):
{ std::unique_lock<std::mutex> lk(m);
  cv.wait(lk, []{ return done; });    // sleeps; wakes only when done==true
}
```

- **Pro:** no busy-wait; the OS parks the Kernal thread until notified. General-purpose,
  works for repeated events too.
- **Con:** you manage three moving parts (mutex + cv + predicate bool) by hand. The
  `wait(lk, predicate)` form is essential — it guards against **spurious wakeups** (a CV can
  wake for no reason; the predicate re-check is why you always pass the lambda). More
  ceremony than a one-shot event needs.

> **Why the predicate lambda matters (depth):** `cv.wait(lk)` with no predicate can return
> even when nothing notified it (spurious wakeup) or if the notify happened *before* you
> started waiting (lost wakeup). The predicate form `cv.wait(lk, pred)` re-checks `pred` in a
> loop, closing both holes. Your `RingBuffer` does exactly this — study those two `wait`
> calls; they're the same pattern you'd use here.

### Mechanism 3 — `std::promise` / `std::future` (best fit for one-shot)

This is C++'s purpose-built tool for "one thread produces a value/event exactly once,
another thread waits for it." Header `<future>`.

```cpp
std::promise<void> indexingComplete;                 // the "sender" end
std::future<void>  ready = indexingComplete.get_future();  // the "receiver" end

// Parser, when done:      indexingComplete.set_value();    // fires the event, once
// Kernal, to wait:        ready.wait();                    // blocks until set_value()
```

- **Pro:** *exactly* models a one-shot event. No manual mutex/cv/flag. `promise<void>` is
  "just signal, no payload"; `promise<T>` can also **carry a value** across the thread
  boundary (e.g., statistics: `promise<size_t>` to pass "indexed 8,412 tokens"). It even
  propagates **exceptions** — if the Parser hits an error, `set_exception()` makes
  `future.get()` rethrow it in the Kernal. That's a clean way to report boot failure across
  threads.
- **Con:** single-use (a promise fires once — which is *exactly* your case, so not really a
  con here). Slightly higher-level machinery to learn, but it's the *right* level.

> **This is the mechanism I'd steer you toward for Question 3.** "The Parser finishes
> building the index exactly once, and the Kernal waits for that one event, possibly
> receiving a result or an error" is the textbook `promise`/`future` scenario. Compare it to
> Go: a `promise<void>`/`future` pair is essentially a `done := make(chan struct{})` where
> the Parser `close(done)` and the Kernal `<-done`. If you've used a done-channel in Go, you
> already understand futures.

### Mechanism 4 — callback via `std::function` (inversion of control)

Instead of the Kernal *waiting*, the Kernal *hands the Parser a function to call* when
done. The Parser doesn't know what it does; it just calls it.

```cpp
// Parser holds:  std::function<void()> onComplete_;   (injected by the Kernal)
// Parser, when done:  if (onComplete_) onComplete_();
// Kernal, at wiring time:  parser.SetOnComplete([&]{ /* start the Engine */ });
```

- **Pro:** decouples beautifully — the Parser knows *nothing* about the Engine or Kernal,
  just "call this when done" (the Part 1 §3 principle, taken all the way). Very extensible:
  swap in different completion behavior without touching the Parser.
- **Con:** the callback runs **on the Parser's thread**, not the main thread. If the
  callback starts the Engine (which spawns a thread and reads stdin), you're now doing
  control-plane work from a data-plane thread — think hard about which thread should own
  that. Often you combine this with Mechanism 3: the callback just does
  `promise.set_value()`, and the *main thread* reacts. Callbacks are powerful but they move
  *where code runs*, which is subtle.

### Mechanism 5 — post an event to a control queue (message passing)

The Parser pushes a "DONE" message onto a dedicated control/event queue that the Kernal
drains. You already have a perfect data structure: `RingBuffer`.

```cpp
// A small event type on an eventQueue the Kernal owns:
// Parser:  eventQueue.push_blocking(Event{EventType::IndexingComplete});
// Kernal:  eventQueue.pop_blocking(ev);  // blocks; then react to ev.type
```

- **Pro:** uniform with the rest of your architecture (everything already talks via
  queues). Scales to *many* event types (IndexingComplete, IndexingFailed, FileError...).
  The Kernal becomes a little event loop — a very "systems" design. Naturally serializes
  events onto the Kernal's thread.
- **Con:** heavier than a one-shot future for a single event. But if you foresee the Kernal
  reacting to *multiple* kinds of lifecycle events, this is the most future-proof and the
  most in keeping with your existing style.

---

## 5. Choosing — a decision table

| If you want... | Use |
|---|---|
| The absolute simplest thing that isn't busy-wait | **CV** (Mechanism 2) |
| The *right* tool for a one-shot "done" (my pick) | **`promise`/`future`** (Mechanism 3) |
| To also pass a result or an error across the thread | **`promise<T>`/`future<T>`** |
| Maximum decoupling of Parser from Kernal | **callback** (Mechanism 4), often + future |
| A general lifecycle-event system, matching your queue style | **event queue** (Mechanism 5) |
| To avoid learning new machinery, and you already loop | atomic flag (Mechanism 1) — last resort |

> **My concrete recommendation for your project:** start with **`std::promise<void>` /
> `std::future<void>`** (Mechanism 3). It's the smallest correct thing that exactly matches
> the shape of your event, it forces you to think about the publish-before-signal ordering,
> and upgrading it to carry stats/errors (`promise<size_t>` or a small result struct) is a
> one-line change later. Once that works, if you find the Kernal needs to react to *several*
> events, graduate to the **event queue** (Mechanism 5) — it reuses your `RingBuffer` and
> matches your architecture.

---

## 6. Where the wait goes, and the thread choreography

The wait belongs in `Kernal::Boot` (Part 3), on the cold path, between starting the Parser
and starting the Engine:

```cpp
// cold path inside Boot():
Start("Dir Reader");
Start("Lexer");
Start("Parser");

ready.wait();          // ← block the MAIN thread here until Parser fires the event
                       //    (optionally: persist the index now — Part 2 §5 — so next boot is warm)

Start("Search Engine");
```

Think about *which thread does what*:

- The **main thread** runs `Boot`, so `ready.wait()` parks the main thread. That's fine —
  during a cold boot there's nothing else for main to do but wait for the index.
- The **Parser thread** fires the event. But note: the Parser thread is still *alive* after
  firing (it may need to exit its loop, forward its own pill if it had downstream consumers,
  etc.). Signalling "done" and the *thread finishing* are separate — don't conflate the
  event with `join()`.

> **A subtle ordering trap:** you must obtain the `future` (`get_future()`) and be ready to
> `wait()` on it *before* the Parser could possibly `set_value()`. If you wire the promise
> too late, the Parser might finish and fire into the void. With `promise`/`future` this is
> actually safe — `set_value()` before `wait()` just makes `wait()` return immediately (the
> state is stored). This is one more reason futures beat a bare condition variable, where a
> notify-before-wait is a *lost wakeup* that hangs you forever. Understand *why* the future
> doesn't have that bug: it stores the "ready" state; a CV notify is fire-and-forget.

---

## 7. Memory visibility — the part beginners miss

Here's the deep systems point. When the Parser builds the index on its thread and the
Kernal (then the Engine) reads it on another thread, **why is the Kernal guaranteed to see
the fully-built index and not a partial one?**

The answer is **happens-before / synchronizes-with** relationships. A correctly-used
synchronization primitive doesn't *just* wake the waiter — it also establishes that
**everything the signalling thread wrote before signalling is visible to the waiting thread
after it wakes.** Specifically:

- `promise::set_value()` **synchronizes-with** `future::wait()/get()`. All writes the Parser
  made before `set_value()` are visible to the Kernal after `wait()` returns. ✅
- `cv.notify` with the mutex, likewise, publishes the writes made under the lock.
- `thread::join()` synchronizes-with the joining thread: after `t.join()` returns, *all* of
  `t`'s writes are visible. (This is why the throwaway-tree fix + a `join` would also
  publish safely — though you want the Engine up *before* shutdown, so `join` isn't your
  signal here.)
- Your `atomic` release/acquire pairs (`store(release)` / `load(acquire)`) do the same at a
  finer grain — that's literally what the `memory_order_release`/`acquire` on `running_` and
  in your `RingBuffer` are *for*.

> **The rule to remember:** *don't* build the index on one thread and read it on another
> connected only by a plain `bool done = true;`. A non-atomic, unsynchronized flag gives you
> **no visibility guarantee** — the reader may see `done == true` but a stale/partial index,
> because the compiler/CPU can reorder the writes. You must use a real synchronization
> primitive (future, CV+mutex, or atomic acquire/release) so that "publish the index" truly
> *happens-before* "read the index." This is the single most important correctness idea in
> the whole series. Re-read `concurrency.md` on memory ordering until this clicks.

---

## 8. Failure paths — because boots fail

Your `promise`/`future` (or event) design should account for the cold path *failing*:

- What if the DirectoryReader found **zero files**? Then indexing "completes" with an empty
  index. Is that success (start Engine, it'll just find nothing) or an error (nothing to
  search)? A design decision — but decide it deliberately.
- What if a subsystem's thread throws? With `promise`, `set_exception()` lets you propagate
  it so `future.get()` rethrows in `Boot`, which returns an `Error` to `main`. Clean.
- What if you `wait()` forever because a pill never reached the Parser (the §2 bug)? This is
  a **boot deadlock**. Consider `future.wait_for(timeout)` (a bounded wait) during
  development so a wiring mistake surfaces as a timeout error instead of a silent hang.
  Production may keep waiting; development wants the diagnostic.

> **Systems insight:** "boot succeeded" and "boot finished" are different. A boot can finish
> in a *failed* state. Your `Boot` returning `Error` (matching `InitAll`/`StartAll`) is the
> right shape — a failed cold start should stop the whole program cleanly, exactly like your
> current `StartAll` rollback does.

---

## 9. The full cold-path choreography (assemble it in your head)

```
Kernal::Boot (main thread)          Parser thread            Lexer / DirReader threads
──────────────────────────          ─────────────            ─────────────────────────
ask Store: index? → NotFound
wire promise/future
Start DirReader ─────────────────────────────────────────►  scan data/, push paths, push pill
Start Lexer ──────────────────────────────────────────────► drain files→lines→tokens,
Start Parser ───────────────────►  pop tokens,               forward pills at end-of-stream
                                    write into Store,
ready.wait()  ....blocked....       pop poison pill,
                                    (index now published)
                                    promise.set_value()  ──► (synchronizes-with wait)
ready.wait() returns ◄──────────────┘
  (optional) persist index to disk
Start Engine ──────────────────────────────────────────────────────────────────────────►  serve queries
```

Every arrow crossing threads must be a real synchronization edge (queue push/pop, or the
promise/future). That's what makes the picture *correct*, not just plausible.

---

## 10. Before you move on

1. Why must you **publish the index before signalling**, and what breaks if you reverse it?
2. Why is `std::promise`/`std::future` a better fit for a *one-shot* "done" than a raw
   `std::condition_variable`? (Hint: notify-before-wait.)
3. What does "synchronizes-with / happens-before" guarantee, and why does a plain
   `bool done = true` fail to provide it?
4. In your pipeline, how many poison pills must reach `lineQueue` (2 workers) and
   `parserQueue` (1 consumer) at end-of-stream, and why?
5. Before signalling is even meaningful, what two data-plane gaps (from Part 1 §7 and §3
   here) must you close first?
6. On which thread does a `std::function` callback run, and why can that be a trap?

Next: **Part 5 — Putting It Together**, the complete boot state machine across all three
questions, a C++ practices checklist, common pitfalls, and a build order so you implement
this without drowning.
