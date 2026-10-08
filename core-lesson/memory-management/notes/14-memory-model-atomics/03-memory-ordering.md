# 14.3 — Memory ordering: happens-before, synchronizes-with, and the three models

This is the heart of the lesson. The orderings decide *which thread sees which writes, when* — and
choosing them correctly is the whole skill of lock-free programming.

## 1. happens-before and synchronizes-with

**happens-before** is the guarantee you're always trying to create. `[CIA §5.3.2]`: it "is the basic
building block of operation ordering in a program; it specifies which operations see the effects of
which other operations." If A happens-before B, then B sees A. Within one thread, **sequenced-before**
(source order) gives happens-before: "if one operation (A) occurs in a statement prior to another (B)
in the source code, then A happens-before B" `[CIA §5.3.2]`.

Across threads you need **synchronizes-with**, which only atomics (or mutexes) create. `[CIA §5.3.2]`:
"if operation A in one thread synchronizes-with operation B in another thread, then A inter-thread
happens-before B," and it composes with sequenced-before transitively: "if A synchronizes-with B and B
is sequenced before C, then A inter-thread happens-before C." That composition is the engine of the
§14.2 fix: `data = 42` is sequenced-before the release; the release synchronizes-with the acquire; the
acquire is sequenced-before `read data` — chain them and `data = 42` happens-before `read data`.

## 2. The six orderings, three models

`[CIA §5.3.3]`: "There are six memory ordering options … `memory_order_relaxed`,
`memory_order_consume`, `memory_order_acquire`, `memory_order_release`, `memory_order_acq_rel`, and
`memory_order_seq_cst`. Unless you specify otherwise … the memory-ordering option for all operations
on atomic types is `memory_order_seq_cst`, which is the most stringent." They form **three models**
`[CIA §5.3.3]`:

| Model | Tags | What it gives |
|---|---|---|
| **sequentially consistent** | `seq_cst` (default) | a single total order **all** threads agree on |
| **acquire-release** | `acquire`, `release`, `acq_rel`, (`consume`) | **pairwise** synchronization, no global total order |
| **relaxed** | `relaxed` | atomicity only — **no** ordering/synchronization |

(Ignore `consume` — it's discouraged and usually treated as `acquire`.)

## 3. Sequentially consistent (the default, the easy one)

`[CIA §5.3.3]`: "the behavior of a multithreaded program is as if all these operations were performed
in some particular sequence by a single thread. This is by far the easiest memory ordering to
understand … all threads must see the same order of operations." You can reason by interleaving: write
out all possible single global sequences, drop the impossible ones. The cost: "it can impose a
noticeable performance penalty, because the overall sequence of operations must be kept consistent
between the processors" — though on x86/x86-64 seq_cst is relatively cheap (a small cost on stores)
`[CIA §5.3.3]`. **Guidance: prototype with `seq_cst`** (it's the default and the safest), then weaken
only where you've shown it's correct and it matters (`[CIA §7.3]`: "use `std::memory_order_seq_cst` for
prototyping").

## 4. Acquire-release (what the ring buffer uses)

`[CIA §5.3.3]`: "atomic loads are acquire operations (`memory_order_acquire`), atomic stores are
release operations (`memory_order_release`), and atomic read-modify-write operations … are either
acquire, release, or both (`memory_order_acq_rel`). Synchronization is **pairwise**, between the thread
that does the release and the thread that does the acquire. **A release operation synchronizes-with an
acquire operation that reads the value written.**"

That one bolded sentence is the whole tool:
- a **`store(release)`** acts as a one-way barrier: everything the thread wrote *before* it cannot be
  reordered after it, and becomes visible to…
- a **`load(acquire)`** that *reads that stored value*: everything *after* the acquire sees everything
  the releasing thread did before its release.

There is **no global total order** (unlike seq_cst) — "different threads can still see different
orderings, but these orderings are restricted" `[CIA §5.3.3]`. You get exactly the producer→consumer
visibility you paid for, and nothing more. That's why it's cheaper and why it's the right tool for a
one-way handoff like a queue.

## 5. Relaxed (atomicity only)

`relaxed` guarantees the operation is atomic and participates in the object's modification order
(§14.1 §3), but creates **no** happens-before with other variables. Use it when the atomic's *value*
is all you need and it isn't publishing other memory — e.g. the free-running counter of §14.2 §4, or
reading/writing *your own* index that the other thread doesn't synchronize through. Misuse it (as a
publish/observe flag) and you reintroduce the §14.1 race.

## 6. The decision rule
- **Publishing data to another thread** (produce an item, set a "ready" flag): `release` on the store,
  `acquire` on the load that observes it. (The ring buffer.)
- **Just a counter / statistic / your own private index**: `relaxed`.
- **Need all threads to agree on a single global order** (e.g. Dekker-style, multiple flags): `seq_cst`.
- **Unsure / prototyping**: `seq_cst` (the default) — correct first, fast later `[CIA §7.3]`.

The ring buffer (§14.4) uses exactly two release/acquire pairs — producer publishes a slot via a
release store to `tail` that the consumer acquires; consumer frees a slot via a release store to `head`
that the producer acquires — and `relaxed` for each thread's reads of *its own* index.

## Drills
1. In the §14.2 fix, label each of the four memory operations (`data=42`, `flag.store(release)`,
   `flag.load(acquire)`, `read data`) with sequenced-before / synchronizes-with, and draw the
   happens-before chain that proves the reader sees 42.
2. Rewrite the fix with `seq_cst` everywhere, then with `relaxed` everywhere. Which still works, which
   breaks, and which is faster on x86? Relate to §3–§5.
3. Give a concrete scenario where acquire-release is insufficient and you truly need seq_cst (hint:
   two flags set by two threads, each thread reads the other's — the store-buffer / IRIW shape).
4. For the ring buffer's producer, why is reading its *own* `tail` safe with `relaxed`, while reading
   the consumer's `head` needs `acquire`? (§6 — which read observes another thread's publish?)

## My summary
