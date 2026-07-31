# Startup & Boot Architecture — Part 3: Conditional Orchestration

> **Answers your Question 2** (and the second half of Q1): *"If the Store says the index is
> empty/missing, the Kernal must kick-start the DirectoryReader → Lexer → Parser pipeline"*
> — and on a warm start, skip straight to the Engine.
>
> This is a **control-plane** doc. The Store already told the Kernal *whether* an index
> exists (Part 2). Now the Kernal must **decide what to do** and **start the right subset of
> subsystems in the right order**. Today it can only `StartAll()`. We fix that — the
> concepts and idioms, not your finished code.

---

## 1. The problem with `StartAll()`

Read `Kernal::StartAll()`. It walks `order_` and starts every subsystem:

```cpp
for (const auto& name : order_) {
    Error error = subsystems_[name]->Start();
    // ... rollback on failure ...
}
```

This is an **all-or-nothing** operation. It's actually *well-designed* for what it does —
note the rollback: if subsystem #4 fails to start, it stops #1–#3 in reverse. That's
proper transactional startup. Keep that quality.

But your desired flow needs three different startup *shapes*:

```
Warm start:   start [Engine]                                  (skip the pipeline)
Cold start:   start [DirReader, Lexer, Parser]  ... later ...  start [Engine]
```

So `StartAll()` is too blunt. You need to start **named subsets**, and you need to start
the Engine **at a different time** than the pipeline. Two capabilities the Kernal lacks.

---

## 2. The core idea: **groups / phases**, not one big list

The cleanest mental model is to stop thinking "start all subsystems" and start thinking
"start a **phase**." A phase is a named group of subsystems that boot together.

```
Phase "pipeline" = { DirReader, Lexer, Parser }
Phase "serving"  = { Engine }
```

Your boot logic then reads almost like English:

```
if index loaded:   start phase "serving"
else:              start phase "pipeline"
                   wait for indexing to finish   (Part 4)
                   start phase "serving"
```

This is how real orchestrators think. Kubernetes has init-containers that must complete
before app containers start. systemd has `Before=`/`After=`/`Requires=` between units.
docker-compose has `depends_on`. They're all expressing *phased, dependency-ordered
startup* — exactly your problem in miniature.

> **Design principle:** the ordering you need isn't "registration order" anymore — it's
> "dependency order." The Engine *depends on* the index being ready. Encode that dependency
> somewhere explicit, don't leave it implicit in a for-loop's iteration order.

---

## 3. Two honest ways to give the Kernal this ability

You have a spectrum from "smallest change" to "most general." Pick based on how far you
want to take the learning. Both are legitimate.

### Approach A — start subsystems by name (minimal, do this first)

Add a method that starts *one named* subsystem (respecting its state machine), and let the
boot logic call it in the right order:

```cpp
// Kernal, conceptual signature:
Error Kernal::Start(const std::string& name);   // start ONE registered subsystem
```

Then your existing `StartAll()` can even be re-expressed in terms of it. The boot decision
becomes:

```cpp
// after InitAll() and after asking the Store:
switch (status) {
  case IndexStatus::Loaded:
      Start("Search Engine");
      break;
  case IndexStatus::NotFound:
  case IndexStatus::Corrupt:
      Start("Dir Reader");
      Start("Lexer");
      Start("Parser");
      // ... Part 4: wait for "done", then:
      Start("Search Engine");
      break;
}
```

- **Pro:** tiny, obvious, reuses everything you have. You already have `GetSubsystem(name)`
  and a `map` keyed by name, so a single-name `Start` is a few lines.
- **Con:** the *ordering knowledge* ("DirReader before Lexer before Parser", "Engine last")
  now lives in this boot function as hand-written call order. That's fine for 5 subsystems.
  It doesn't scale to 50, but you don't have 50.

> **Watch the rollback.** `StartAll()` rolls back on failure. If you start subsystems one by
> one, *you* are now responsible for stopping the already-started ones if a later Start
> fails. Think about how you'd track "what have I started so far" so you can unwind it —
> `StartAll()`'s local `started` vector is the pattern to copy.

### Approach B — declare dependencies, let the Kernal compute the order (general)

Give each subsystem a list of names it depends on, and have the Kernal **topologically
sort** them to compute a valid start order automatically.

```
DirReader → (no deps)
Lexer     → depends on DirReader        (needs its queue producing)
Parser    → depends on Lexer
Engine    → depends on "index ready"
```

- **Pro:** the dependency graph is *declared once* next to each subsystem; the Kernal
  figures out order. Adding a subsystem doesn't mean editing a hand-written sequence. This
  is how the grown-up orchestrators do it.
- **Con:** you must implement a topological sort (and detect cycles — a dependency cycle
  is a bootstrap deadlock, and a good sort *reports* it rather than hanging). More C++, more
  concepts (graphs, DFS/Kahn's algorithm). Great learning, but do **Approach A first** so
  you have a working boot before you generalize.

> **Recommendation:** implement **A** now. Once your conditional boot works end-to-end,
> come back and refactor to **B** as a deliberate exercise in graph algorithms. Don't build
> the topological sort before you've felt the pain that motivates it — that's how you
> actually learn *why* it exists.

---

## 4. The C++ practice: modeling the decision with `enum class` + `switch`

Part 2 gave you `enum class IndexStatus`. The Kernal's decision point should be a `switch`
over it. Why a `switch` and not `if/else` chains?

```cpp
switch (status) {
    case IndexStatus::Loaded:   /* warm */ break;
    case IndexStatus::NotFound: /* cold */ break;
    case IndexStatus::Corrupt:  /* cold */ break;
}
```

- With a **scoped enum**, if you later add `IndexStatus::Rebuilding`, most compilers
  (`-Wswitch`) will **warn you** that your `switch` doesn't handle it. The type system nags
  you into completeness. An `if/else` chain silently falls through the gap. This is a real,
  practical reason to prefer enums + switch for state-driven control flow.
- It reads as a **decision table**, which is exactly what a boot decision is.

> **Go contrast:** this is your `switch` on a typed constant, like switching on an iota-based
> constant in Go — but C++'s compiler exhaustiveness warnings give you a safety net Go's
> `switch` doesn't (Go won't warn about a missing case). Lean on it.

---

## 5. Where does the boot decision *live*?

Candidate homes, and the trade-offs:

1. **In `main.cpp`.** Currently `main` drives `InitAll`/`StartAll`/`StopAll`. You *could*
   put the `switch` there: `main` asks the Store, then calls the Kernal's per-name `Start`.
   - Pro: no new Kernal method beyond `Start(name)`; the policy is visible at the top level.
   - Con: `main` becomes the orchestrator. That's OK for a small program, but it leaks
     control-plane logic out of the control plane.

2. **In a new Kernal method**, e.g. `Kernal::Boot(Store& store, path indexPath)`.
   - Pro: the Kernal *is* the control plane (Part 1 §3). Boot policy belongs there. `main`
     shrinks to `kernal.Boot(...)`. The Kernal owns the whole lifecycle: Register → Init →
     **Boot (decide + start)** → Stop.
   - Con: the Kernal now needs to know about `Store` and `IndexStatus`. That coupling is
     acceptable — the control plane is *supposed* to know how to orchestrate the data plane.

> **Recommendation:** a `Kernal::Boot(...)` method. It keeps `main` thin and puts the
> decision in the plane that owns decisions. It also gives you one obvious place to add the
> "wait for indexing to finish" step from Part 4.

> **Your turn:** sketch the signature of `Boot`. What does it need as inputs? (It needs to
> *reach* the Store to ask it, and it needs the index path.) What does it return? (An `Error`,
> like the other lifecycle methods — so `main` can report a failed boot.) Notice you already
> have `GetSubsystem("Store")` + `dynamic_cast<Store*>` in `main.cpp` — that same trick lets
> the Kernal get a typed `Store*` from its own map.

---

## 6. The `dynamic_cast` you'll need, and what it means

In `main.cpp` you already wrote:

```cpp
Store* store = dynamic_cast<Store*>(kernal.GetSubsystem("Store"));
```

Understand what this is doing, because your Kernal will do the same to *ask* the Store:

- `GetSubsystem` returns a `Subsystem*` — the **base-class** pointer. Through it you can
  only call `Subsystem`'s virtual interface (`Name`, `Init`, `Start`, `Stop`, `GetState`).
  You *cannot* call `LoadIndexIfPresent` — that's a `Store`-specific method the base doesn't
  know about.
- `dynamic_cast<Store*>` asks at **runtime**: "is this `Subsystem` actually a `Store`?" If
  yes, you get a usable `Store*`; if no (you passed the wrong name), you get `nullptr`.

```cpp
Subsystem* s = GetSubsystem("Store");
Store* store = dynamic_cast<Store*>(s);
if (!store) return Error("Store not registered or wrong type");  // ALWAYS check
```

> **C++ depth:** `dynamic_cast` works because `Subsystem` has virtual functions, which makes
> it *polymorphic* — the object carries runtime type info (RTTI). A downcast that isn't
> valid returns `nullptr` for pointers (or throws for references). Always null-check the
> result; treating a failed cast as success is a classic crash. (This is roughly Go's
> `store, ok := s.(*Store)` type assertion — the `ok` is your null check.)

> **Design smell to notice:** if you find yourself `dynamic_cast`-ing all over the place,
> it's often a sign the base interface is missing something. That's a real tension worth
> sitting with — but for asking the one concrete Store its one concrete question at boot,
> a single checked downcast is fine and idiomatic.

---

## 7. The warm path is genuinely simpler — respect that

On a warm start you literally just start the Engine. The pipeline subsystems
(DirReader/Lexer/Parser) are **registered and initialized but never started**. Think
carefully about whether that's OK:

- Is it safe for a subsystem to sit in `INITIALIZED` forever and never reach `STARTED`?
  Look at your `Subsystem` state machine and `~Kernal()`. The destructor only force-stops
  things in `STARTED`. Something in `INITIALIZED` just... never starts, never needs
  stopping. That's fine. **Not starting is a valid outcome**, not a bug.
- Do the pipeline subsystems hold resources that leak if never started? Check their
  constructors and `OnInit`. DirReader/Lexer/Parser only capture queue references and, in
  `OnInit`, do validation — no threads, no files held open. So skipping their `Start` costs
  nothing. Good.

> **Systems insight:** "the fast path does less work" is the entire point of a warm start.
> If your warm path started the pipeline too, you'd be doing the expensive indexing you
> were trying to avoid. Verify your warm path really is lean.

---

## 8. Don't forget: `StopAll()` still has to be correct for both paths

Whatever you start, you must be able to stop. Re-read `Kernal::StopAll()` — it iterates
`order_` in **reverse** and calls `Stop()` on everything. Here's the good news: `Stop()` in
your `Subsystem` base is already **idempotent-ish**:

```cpp
Error Stop() {
    if (state_ != State::STARTED) {
        std::cout << "... Not started, skipping stop ...";
        return Error("");   // ← safe no-op if it was never started
    }
    ...
}
```

So on a warm start, `StopAll()` will try to stop DirReader/Lexer/Parser, see they're not
`STARTED`, and skip them harmlessly. **Your conditional startup doesn't break shutdown** —
because the state machine guards each transition. This is the payoff of the disciplined
lifecycle you already built. Appreciate it: a sloppier design would crash trying to
`join()` a thread that was never created.

> **Your turn:** trace, on paper, a warm-start run through `StopAll()`. Confirm each
> not-started subsystem hits the `state_ != STARTED` guard and no `join()` is attempted on a
> default-constructed `std::thread`. (What happens if you `join()` a thread that was never
> started? Look it up — it's undefined-behavior-adjacent, and the guard is what saves you.)

---

## 9. The shape of the cold path — and the cliffhanger

The cold path has a step the warm path doesn't: **wait**.

```cpp
// cold start, conceptually:
Start("Dir Reader");
Start("Lexer");
Start("Parser");

// ??? — how does Boot() know indexing is FINISHED before starting the Engine?
waitForIndexingComplete();     // ← this is the whole of Part 4

Start("Search Engine");
```

If you start the Engine *immediately* after starting the Parser, the Engine will accept
queries against an index that's still being built (or empty) — a race. The Kernal must
**block until the Parser signals completion**. That signal — how the Parser tells the
Kernal "I've consumed the poison pill and the index is built" — is your Question 3, and it
is the subject of **Part 4**.

---

## 10. Before you move on

1. Why is `StartAll()` too blunt for your flow, and what does "phase" or "group" give you?
2. What's the trade-off between starting subsystems by name (Approach A) vs declaring
   dependencies and topologically sorting (Approach B)? Which should you build first, and
   why?
3. Why prefer `enum class` + `switch` over `if/else` for the boot decision?
4. What does `dynamic_cast<Store*>` do, why must you null-check it, and what makes it
   possible at all?
5. Why does your existing `StopAll()` remain correct even when you only started a subset?
6. Why *must* the cold path wait before starting the Engine?

Next: **Part 4 — Completion Signalling**, the menu of C++ mechanisms for the Parser to tell
the Kernal "indexing is done," and how to hand the built index into the Store safely.
