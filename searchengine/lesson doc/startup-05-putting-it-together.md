# Startup & Boot Architecture — Part 5: Putting It Together

> The capstone. Parts 1–4 taught the pieces; this doc assembles them into **one boot state
> machine**, gives you a **C++ practices checklist**, a **build order** so you don't drown,
> and a **pitfalls list**. Still no finished feature code — this is the map you build from.

---

## 1. The complete boot state machine

Here is the whole thing across all three of your questions, as one diagram. Every box is a
state; every arrow is a transition your Kernal drives.

```
        ┌───────────────────────────────────────────────────────────────┐
        │ REGISTER: main creates queues + subsystems, Kernal takes owner │
        └───────────────────────────────┬───────────────────────────────┘
                                         ▼
        ┌───────────────────────────────────────────────────────────────┐
        │ INIT ALL: cheap pre-flight for every subsystem (fail fast)     │  ← already exists
        └───────────────────────────────┬───────────────────────────────┘
                                         ▼
        ┌───────────────────────────────────────────────────────────────┐
        │ ASK STORE:  IndexStatus = Store.LoadIndexIfPresent(path)       │  ← Q1 / Part 2
        └───────────────────────────────┬───────────────────────────────┘
                                         ▼
                          ┌──────────────┴───────────────┐
                 Loaded   │                              │  NotFound / Corrupt
              (WARM)       ▼                              ▼   (COLD)
        ┌────────────────────────┐         ┌────────────────────────────────────┐
        │ (index already in Store)│         │ Start DirReader → Lexer → Parser    │  ← Q2 / Part 3
        └───────────┬────────────┘         │ (Parser writes into Store)          │
                    │                       └──────────────────┬─────────────────┘
                    │                                          ▼
                    │                       ┌────────────────────────────────────┐
                    │                       │ WAIT for Parser "indexing complete"  │  ← Q3 / Part 4
                    │                       │ (promise/future). Publish→signal.    │
                    │                       └──────────────────┬─────────────────┘
                    │                                          ▼
                    │                       ┌────────────────────────────────────┐
                    │                       │ (optional) PERSIST index to disk    │  ← Part 2 §5
                    │                       │ so the NEXT boot is warm             │
                    │                       └──────────────────┬─────────────────┘
                    └───────────────────┬──────────────────────┘
                                        ▼
        ┌───────────────────────────────────────────────────────────────┐
        │ START Engine → READY: accept search queries                    │
        └───────────────────────────────┬───────────────────────────────┘
                                         ▼
        ┌───────────────────────────────────────────────────────────────┐
        │ RUN until user exits (std::cin.get)                            │
        └───────────────────────────────┬───────────────────────────────┘
                                         ▼
        ┌───────────────────────────────────────────────────────────────┐
        │ STOP ALL in reverse; state-machine guards skip not-started     │  ← already exists
        └───────────────────────────────────────────────────────────────┘
```

Notice how little of this is *new machinery* — REGISTER, INIT ALL, STOP ALL, the subsystem
state machine, and the queues all already exist. Your three questions add exactly three
things: **ask the Store**, **branch + start subsets**, **wait for completion**. That's the
whole project. Keeping it that small is the sign your existing architecture was well chosen.

---

## 2. Map each question to a plane, a file, and a mechanism

| Your question | Plane | File(s) you'll touch | Core C++ mechanism |
|---|---|---|---|
| Q1: does a saved index exist? | data → reports to control | `store.hpp/.cpp` (+ new load method); Kernal calls it | `std::filesystem`, `<fstream>` RAII, `enum class IndexStatus` |
| Q2: branch + kick-start pipeline | control | `kernal.hpp/.cpp` (new `Boot`/`Start(name)`); `main.cpp` shrinks | `switch` on enum, per-name start, `dynamic_cast<Store*>` |
| Q3: Parser signals completion | data → wakes control | `parser.hpp/.cpp`, `lexer.cpp` (pill forwarding), `kernal.cpp` | `std::promise`/`std::future` (+ pill plumbing, `Store*` in Parser) |

---

## 3. The C++ practices checklist (the "deep C++" payoff)

These are the idioms this project should cement. Tick each off as you internalize it.

**Ownership & lifetime**
- [ ] The Kernal owns subsystems via `unique_ptr` (single owner, auto-freed). You never
      `delete` a subsystem yourself. (See `Register`, `~Kernal`.)
- [ ] `main` owns the queues; subsystems hold **references** to them. The queues must
      **outlive** every subsystem — which they do, because `main`'s locals outlive the
      Kernal declared after... wait: check the declaration order in `main.cpp`. (`kernal` is
      declared *before* the queues. Think hard: does the Kernal's destructor, which may
      `Stop()` threads that touch the queues, run *before or after* the queues are
      destroyed? Destruction is reverse declaration order. This is a real lifetime question
      — trace it. See `ownership-and-lifecycle-part*.md`.)

**RAII**
- [ ] Files (`ifstream`/`ofstream`) close themselves on scope exit — you never call
      `close()`. (Part 2, `cpp-file-io.md`.)
- [ ] `std::lock_guard`/`std::unique_lock` unlock on scope exit — you never call `unlock()`
      manually (except the deliberate early `lock.unlock()` in your `RingBuffer`).

**Type-driven design**
- [ ] Use `enum class` for closed sets of outcomes (`IndexStatus`), not `int` codes.
- [ ] Use `switch` over the enum and let `-Wswitch` enforce exhaustiveness.
- [ ] Use `std::optional<T>` for "maybe a value"; a richer result type when you need
      "value *or* reason it's absent."
- [ ] Return big read-only things by `const&`; mark non-mutating methods `const`.

**Polymorphism**
- [ ] Understand the **template-method pattern** in `Subsystem`: public `Init/Start/Stop`
      guard state, protected virtual `OnInit/OnStart/OnStop` do the work. You override the
      `On*` hooks, never the guards.
- [ ] Understand `dynamic_cast` for a checked downcast from `Subsystem*` to `Store*`, and
      always null-check it.

**Concurrency**
- [ ] `std::thread` lifecycle: create in `OnStart`, `join()` in `OnStop`, guard with
      `joinable()`. Never destroy a joinable thread (it `std::terminate`s).
- [ ] Atomics with explicit `memory_order` (`acquire`/`release`) for flags shared across
      threads — never a plain `bool` for cross-thread signalling.
- [ ] `promise`/`future` for one-shot cross-thread events; CV+mutex+predicate for repeated
      ones; understand spurious/lost wakeups.
- [ ] **happens-before**: a real sync primitive publishes *all* prior writes to the waiter.
      This is why the built index is safely visible after the completion signal.

---

## 4. Build order — do it in this sequence (don't skip ahead)

Trying to build all three questions at once will drown you. Here's a dependency-respecting
order where **each step leaves the program runnable** so you can test as you go.

1. **Make the Parser actually build an index in the Store.** (Part 1 §7, Part 4 §3.)
   - Give the Parser a `Store*` (constructor, like `Engine`).
   - Decide how the document name reaches the Parser (change `parserQueue_`'s element type,
     or send token+doc). Fix the throwaway-`BSTree` loop.
   - Test: after a run, is `searchIndex_` non-empty? Does a search now find something?
   *(No new boot logic yet — just close the data gap. Everything else depends on this.)*

2. **Make the poison pill reach the Parser during *normal* completion.** (Part 4 §2.)
   - DirReader → Lexer coordinator already works. Forward pills to the Lexer workers and
     then to the Parser at end-of-stream (not only in `OnStop`).
   - Test: on a normal run, does the Parser print "Received end signal" *without* you
     triggering shutdown?

3. **Add the completion signal.** (Part 4, Mechanism 3.)
   - Wire a `promise<void>`/`future<void>`. Parser `set_value()` after the pill; something
     in the control path can `wait()`.
   - Test: a temporary `future.wait()` in `main` after start proves the signal fires.

4. **Add persistence.** (Part 2.)
   - `Store::LoadIndexIfPresent(path)` returning `IndexStatus`; a save function used after
     a cold build.
   - Test independently: save after a cold run, then confirm the file exists and reloads
     into an equivalent map.

5. **Add the conditional boot.** (Part 3.)
   - `Kernal::Boot`: ask the Store, `switch`, start the pipeline *or* start the Engine, and
     on the cold path `wait()` then start the Engine.
   - Add per-name `Start` (Approach A) with its own rollback tracking.
   - Test both paths: delete the index file → cold start builds it; run again → warm start
     skips the pipeline and the Engine comes up instantly.

6. **(Stretch) Generalize** to declared dependencies + topological sort (Part 3, Approach
   B). Only after 1–5 work.

> Steps 1–2 are pure data-plane plumbing with **no concurrency signalling** — get them
> rock-solid first. Steps 3–5 are the orchestration. This ordering means you're never
> debugging a race and a data-modeling bug at the same time.

---

## 5. Pitfalls checklist (the bugs this design invites)

- **Starting the Engine before the index is ready.** The whole reason Q3 exists. On the
  cold path, `wait()` *must* precede `Start("Search Engine")`.
- **Publishing after signalling.** Set the index into the Store *before* `set_value()`, or
  the Engine may read a partial index. (Part 4 §1, §7.)
- **Wrong pill count.** N consumers on a queue need N pills. Two Lexer workers → two
  line-queue pills. One short and a thread hangs forever on `pop_blocking`.
- **`join()`ing a never-started thread / not joining a started one.** The state-machine
  guards protect you *if* you respect them — don't `join` outside the `state_ == STARTED`
  path, and never let a joinable thread's destructor run.
- **Unsynchronized `bool done`.** No happens-before → stale index visible. Use a real
  primitive.
- **Unchecked `dynamic_cast`.** A wrong name gives `nullptr`; dereferencing it crashes.
- **`exists()` then open — TOCTOU + open-can-still-fail.** Always check `is_open()` too.
- **Trusting a present-but-corrupt index.** Validate; fall back to cold start when unsure.
- **Lifetime inversion between `kernal` and the queues in `main.cpp`.** Verify destruction
  order (§3). If threads outlive the queues they reference, that's a use-after-free.
- **Lost wakeup with a bare condition variable.** Notify-before-wait hangs; use the
  predicate form, or prefer `promise`/`future` which stores the state.
- **Boot deadlock on a wiring bug.** During dev, use `wait_for(timeout)` so a missing pill
  surfaces as a diagnostic, not a hang.

---

## 6. How this maps to the real world (why it was worth it)

What you've built, in the vocabulary of the systems you'll work on:

- **Warm/cold start** = Redis RDB load vs empty, Postgres data dir vs `initdb`, a build
  cache hit vs miss.
- **Control plane vs data plane** = Kubernetes control plane vs workloads; an OS kernel
  scheduling vs processes computing.
- **Phased, dependency-ordered startup** = systemd units, k8s init-containers,
  docker-compose `depends_on`.
- **Poison pill / end-of-stream** = closing a Go channel, EOF on a pipe, a sentinel in a
  message stream, Kafka end-of-partition markers.
- **Completion signalling + happens-before** = `sync.WaitGroup`/done-channel in Go,
  `Future`/`CompletableFuture` in Java, `promise`/`future` everywhere.
- **Serialization + magic-number/version header** = every file format, every database page,
  every save file, every network protocol.

The search engine is the *excuse*. The transferable skill is: **orchestrating the startup
of a concurrent system that persists and resumes state.** That skill shows up in every
database, every service runtime, every game engine, every embedded controller you'll ever
touch.

---

## 7. Final self-check

If you can do all of these from memory, you've got the deep understanding you asked for:

1. Draw the full boot state machine (§1) on a blank page.
2. For each of your three questions, name the plane, the file(s), and the C++ mechanism.
3. Explain why the index must be published *before* the completion signal, in terms of
   happens-before.
4. Explain why `StartAll()` is too blunt and what "phase" gives you.
5. Explain why `promise`/`future` beats a bare condition variable for a one-shot event.
6. Explain why a warm start is both faster *and* the reason you persist the index at all.
7. List three real systems that do the exact same warm/cold-start dance and say what plays
   the role of your "index file" in each.

When those are second nature: stop reading, open `store.hpp`, and start on **Build Order
step 1**. The docs have done their job — the rest is yours to write.
