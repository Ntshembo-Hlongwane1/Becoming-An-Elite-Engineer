# 09 — The Unified Operational Flow (Startup × Frontier)

> **The true capstone.** The `startup-01..05` series changed how your engine **boots** (warm
> vs cold start, persistence, conditional orchestration, the Parser→Kernal completion
> signal). This frontier series changed **what the index *is*** (an in-RAM map → on-disk
> immutable segments). Those two sets of changes **touch the same seams** — and if you apply
> them independently you'll build contradictions. This doc reconciles them into **one
> operational flow**, walks the changes **step by step with a "why" for each**, and folds the
> two build orders into a single master sequence.
>
> **Read this last.** It assumes both series. Every claim points back to a section so you can
> re-derive it. The goal: after this, you can draw the whole engine — boot to query — and
> defend *why* each box exists.

---

## 1. The core realization: your two sets of changes collide (on purpose)

The startup docs and the frontier docs are not two separate projects. They are **two views
of the same seams.** Where they meet, the frontier concept *supersedes and enriches* the
startup concept — it doesn't replace the orchestration, it fills in what was being
orchestrated.

| Seam | What `startup` had you build | What `frontier` turns it into | Net truth |
|---|---|---|---|
| The index itself | Persist `Store.searchIndex_` (a map) to a file (`startup-02`) | An immutable **segment** (postings + term dict) + a **manifest** (`frontier-02/03`) | The "persisted map" **is** a segment; the file format is the frontier format |
| "Does a saved index exist?" | `Store::LoadIndexIfPresent → IndexStatus` (`startup-02 §7`) | Read the **manifest/commit point**; mmap listed segments (`frontier-03 §4`) | Same tri-state (Loaded/NotFound/Corrupt), now backed by an atomic commit |
| Cold-start pipeline ends | Parser signals "done" → Kernal starts Engine (`startup-04`) | ... → **FLUSH** buffer→segment → **COMMIT** manifest → *then* Engine (`frontier-03 §2-4`) | The signal now gates persist-then-serve, not just serve |
| Parser writes results | Parser writes into `Store` (fix throwaway BST) (`startup-01 §7`) | Parser (indexer) appends to the **in-memory indexing buffer** that gets flushed (`frontier-03 §2`) | `Store`'s mutable map **is** the indexing buffer; the segment is its durable form |
| Poison pill at end-of-stream | Pill must reach Parser during normal run (`startup-04 §2`) | End-of-stream = the trigger to **flush the buffer** (`frontier-03 §2`) | One event, two duties: terminate pipeline **and** flush |
| Warm path | Start **only** the Engine (`startup-03 §7`) | Engine **opens manifest + mmaps segments**, ready (`frontier-02 §4`) | Same control flow, richer meaning (reads segments, not a RAM map) |
| Background work | (not in startup) | Refresh/Flush/Merge workers (`frontier-07`) | New **`Subsystem`s registered with your `Kernal`** — same lifecycle |
| Lifetime discipline | `kernal` vs queues destruction order (`startup-05 §3`) | + segment snapshots, mmap handles, refcount retire (`frontier-03 §8`) | Same "who outlives whom" rule, now over mmap regions too |

**Read that table twice.** It is the whole doc in miniature: every startup change has a
frontier evolution, and knowing the evolution stops you from building the naive version and
then fighting it later.

---

## 2. The unified boot flow (the operational spine)

Here is your `Kernal::Boot` (from `startup-03`) with the frontier reality filled in. Both
paths, one diagram:

```
 main: create queues + subsystems + background workers → Kernal.Register(...) → InitAll()
        │                                                 (startup-03; frontier-07 workers too)
        ▼
 Kernal::Boot():
   status = Store.OpenIndex(manifestPath)          ← was LoadIndexIfPresent (startup-02 §7)
        │                                             now: read manifest → mmap segments (frontier-03 §4)
        ├───────────────── Loaded (WARM) ─────────────────┐
        │                                                 │
        │   (segments already mmap'd; nothing to build)   │
        │                                                 ▼
        │                                        Start("Search Engine")   ← only the Engine (startup-03 §7)
        │                                        Engine serves over the open segments
        │
        └───────────────── NotFound / Corrupt (COLD) ─────┐
                                                          ▼
              Start("Dir Reader"); Start("Lexer"); Start("Parser(Indexer)")  (startup-03; frontier write path)
                          │  pipeline fills the IN-MEMORY indexing buffer (frontier-03 §2)
                          │  end-of-stream poison pill reaches Parser (startup-04 §2)
                          ▼
              Parser: buffer complete → set_value() on the completion promise (startup-04, mech 3)
                          │
              Kernal wakes from ready.wait()  ← the one-shot signal (startup-04 §6)
                          │
              Kernal: FLUSH buffer → segment file (postings/term-dict/…)  (frontier-03 §2, 04)
              Kernal: COMMIT → write manifest atomically (rename)          (frontier-02 §4, 03 §4)
                          │   ← why here: persist BEFORE serving so next boot is WARM (startup-02 §5)
                          ▼
              Start("Search Engine")   → Engine opens the just-committed manifest, mmaps, serves
        ┌─────────────────────────────────────────────────┘
        ▼
 RUN (std::cin loop) → StopAll() in reverse; state guards skip not-started (startup-03 §8)
```

Three things to notice, each a *why*:

- **The completion signal now gates persist-then-serve, not just serve.** In `startup-04`
  the promise/future meant "index built → start Engine." Now there's work *between* the
  signal and the Engine: **flush + commit.** *Why:* the frontier index isn't done when it's
  in RAM — it's done when it's a committed segment. Serving before commit would mean a crash
  loses the index *and* the next boot goes cold again (`startup-02 §5`'s "persist so next boot
  is warm" is the reason).
- **Who does the flush/commit?** The **Kernal** (control plane), *after* `wait()`, not the
  Parser. *Why:* `startup-01 §3` — the data-plane Parser produces a *fact* ("buffer ready");
  the control-plane Kernal makes the *decision* ("persist it, then serve"). Committing is a
  lifecycle decision; it belongs in the Kernal. (Alternatively the Parser flushes and the
  Kernal only commits — either is defensible; the *principle* is what matters: control plane
  owns the commit.)
- **Warm path is unchanged in shape, richer in meaning.** Still "start only the Engine"
  (`startup-03 §7`), but "start" now means *open manifest + mmap*, and opening a 50 GB index
  is near-instant (`frontier-02 §4`) instead of loading a map into RAM. *Why the warm path
  exists at all:* skip the expensive pipeline (`frontier` docs' whole cold cost).

---

## 3. What `Store` becomes (the single most important reconciliation)

In `startup`, `Store` was one thing: a mutable `unordered_map` you fill and persist. In the
unified engine it **splits into two responsibilities**, and seeing this split is the crux:

```
 Store (write side)  = the in-memory INDEXING BUFFER
     · Parser/Indexer appends postings here during a cold build   (frontier-03 §2)
     · mutable, transient — exists only until flushed
 Store (read side)   = the OPEN SEGMENTS + manifest snapshot
     · immutable, mmap'd; what the Engine queries                  (frontier-03 §8)
     · a shared_ptr snapshot so readers are safe during merge      (frontier-03 §8 code)
```

*Why the split:* the `startup` map conflated "the thing being built" with "the thing being
queried." The frontier model insists they're different — the build target is **mutable and
throwaway**, the query source is **immutable and durable**. The flush is the moment one
becomes the other. Your `Store` should expose a write API (append to buffer) and a read API
(query the current segment snapshot), and they should not be the same data structure.

> **This retro-fixes the `startup-01 §7` bug elegantly.** The throwaway `BSTree` in
> `Parser::Run` wasn't just "not wired to Store" — it was the *right instinct* (a build-time
> structure) built in the *wrong place* (locally, per-iteration). The frontier answer: the
> Parser appends into the **shared indexing buffer**; that buffer is flushed to a segment;
> the local tree disappears. You'll delete that `BSTree` and replace it with "append to the
> buffer."

---

## 4. The unified serve flow (your original Q2/Q3, made concrete)

Your very first question was: *"hit TopK first; if nothing, fan out Trie + B+Tree and
consolidate; keep TopK fresh."* Here is that intent, reconciled with everything, as the
**one concrete request path** your engine implements:

```
 query string
   │
 [CACHE]     request/filter cache lookup (segment-scoped key)      ← your "hit TopK first"
   │          hit → return; miss → continue                          (frontier-07 §6)
   ▼          why: cheap hot-path; freshness via immutability+epoch, not manual update (Q3)
 [ANALYZE]   SAME analyzer as indexing (the extracted Lexer)        (frontier-05 §2)  ← the FLAG
   ▼          why: query terms must byte-match indexed terms
 [PARSE]     → BOOL{ MUST text:… ; FILTER price<… ; FILTER stock:in }(frontier-05 §3)
   ▼
 [REWRITE]   prefix → terms via Trie/FST ; range → point iterator   (frontier-05 §4)
   ▼          why: "Trie fires" here as EXPANSION, not a parallel racer (your reframe)
 [PER-SEGMENT] (parallel across segments — the REAL fan-out)         (frontier-05 §5)
   │   candidates = leapfrog-AND( postings(term) , filterBitset(price ∧ stock) )
   │   skip lists skip blocks; BM25 scores survivors; BlockMax WAND prunes (04 §6, 06 §5)
   │   feed size-K MIN-HEAP  ← your "TopK data structure" (ephemeral, per-query) (06 §4)
   ▼          why: consolidation = postings/segment merge, NOT racing Trie vs B+Tree
 [MERGE]     merge per-segment top-Ks → global top-K docIDs          (frontier-05 §7)
   ▼
 [RESCORE]   re-rank top-N by business rules (rating, stock, margin) (frontier-06 §7)
   ▼          why: relevance retrieves; merchandising re-ranks a tiny survivor set
 [FETCH]     docID→filepath table → read stored record for final K   (frontier-05 §7) ← the FLAG
   ▼          why: fat records read only for the K shown, not every candidate
 [CACHE-PUT] store result under segment-scoped key                   (frontier-07 §6)
   ▼
 results → user
```

The three pieces you originally named are all here — just placed where they *truly* belong:
- **"TopK first"** = the **request cache** (07 §6), kept fresh by immutability + epochs (Q3
  answered), *not* a maintained TopK.
- **Trie + B+Tree** = **rewrite (Trie/FST) → retrieve (postings) + filter (attribute
  B+Tree/points)** — a **pipeline**, and the fan-out is across **segments** (05 §5, §7).
- **TopK** = the **ephemeral size-K min-heap** in ranking (06 §4).

Two of the boxes are the **prerequisites I flagged**: the shared analyzer (ANALYZE) and the
docID→filepath table (FETCH). They're not optional garnish — the flow literally can't run
without them, which is *why* the flag mattered.

---

## 5. The master build order (both series folded into one sequence)

`startup-05` gave a 6-step order; `frontier-08` gave a 10-stage order. They interleave.
Here is the **single sequence**, each step noting which doc it comes from and **why it's
placed there**. Every phase leaves a runnable engine.

| # | Do this | From | Why here (dependency) |
|---|---|---|---|
| **P0** | Give Parser a `Store*`; **dense uint32 docIDs** + `docID→filepath` table; carry doc name through the queue; **append postings into the shared buffer** (delete throwaway BST); pill reaches Parser at end-of-stream | startup 1-2 + frontier stage 1 | Nothing indexes correctly until the pipeline produces a real inverted index in shared state, keyed by ints. Foundation of *everything*. |
| **P1** | Serialize the buffer to a **segment file** (sorted postings, delta+VByte); write a **manifest** with an **atomic commit** | frontier stages 2-3 | `startup`'s "persist the index" is *realized* as the segment+manifest — do it the frontier way from the start so warm boot isn't a naive RAM reload. |
| **P2** | `Store::OpenIndex(manifest) → IndexStatus`; wire **promise/future**; **`Kernal::Boot`** switches warm/cold; cold = pipeline → **flush → commit** → start Engine | startup 3-5 + P1 | This is the operational spine (§2). It needs P0 (a real buffer) and P1 (a segment+manifest) to exist first. |
| **P3** | **Extract the Lexer's analysis** into a shared function; **BM25 + top-K heap + norms** | frontier stage 4 (the FLAG) | Ranked results. Needs P0's docIDs (for the heap) and the docID→filepath table (for FETCH). |
| **P4** | **Blocks(128) + FOR + skip lists**; then **per-block impacts + WAND/BlockMax WAND** | frontier stages 5,7 | Speed. Needs P1's postings on disk and P3's scores (impacts are max *scores*). |
| **P5** | **Attribute index** (B+Tree/points) on price/stock/rating + **filter bitset cache**; **sort modes** + **rescoring** | frontier stages 6,10 | Your inventory superpowers. Needs P3 (scores) + P2 (segments to attach attribute indexes to). |
| **P6** | **Trie/FST prefix** suggester | frontier stage 8 | Autocomplete. Independent; slots in once the term dictionary exists (P1). |
| **P7** | **Multiple segments + background Refresh/Flush/Merge workers as `Kernal` subsystems** | frontier stage 9 + doc 07 | Scale + NRT. Needs P2's commit + P1's segments; reuses the `Kernal` lifecycle you already built. |

> **Why this order and not the frontier order alone:** because your *orchestration* changes
> (P0–P2) must land before the *index-quality* changes (P3–P7). You can't test a warm/cold
> boot decision (P2) until the pipeline writes a real index (P0) that can be persisted (P1).
> The startup series is the skeleton; the frontier series is the muscle. Skeleton first.

---

## 6. The handful of "why"s that, if you get them, mean you understand the whole thing

Each of these is a decision that *changed* because the two series met. Be able to explain
each cold:

1. **Why persist as a segment, not a `map` dump?** A map dump reloads fully into RAM on warm
   start — O(index size) work and memory before you can serve. A segment is **mmap'd**: open
   is near-instant, hot pages fault in lazily (`frontier-02 §4`, `01 §3`). The startup warm
   start's *promise* (fast resume) is only *delivered* by the segment format.

2. **Why does the completion signal now trigger flush + commit before the Engine?** Because
   "built in RAM" ≠ "durable + point-in-time." Serving before commit risks a crash losing the
   index and re-going-cold next boot. Commit's atomic manifest swap (`frontier-03 §4`) is what
   makes the served index a consistent snapshot. (`startup-02 §5` demanded persistence; here's
   *where* it fires.)

3. **Why is end-of-stream both "terminate" and "flush"?** The poison pill means "no more
   documents in this batch." That is *exactly* the condition under which the buffer is
   complete and should become a segment. One event, because they're the same fact
   (`startup-04 §2` + `frontier-03 §2`).

4. **Why does `Store` split into buffer (write) + segments (read)?** Because building and
   querying have opposite needs: the build target must be **mutable**; the query source must
   be **immutable** (for lock-free reads, safe merges, cacheable filters — `frontier-03 §8`,
   `07 §6`). The flush is the hand-off. Conflating them (the `startup` map) is the root of the
   throwaway-tree confusion.

5. **Why are background workers `Kernal` subsystems?** Because refresh/flush/merge have the
   *same lifecycle needs* (init, start a thread, stop/join, ordering, rollback) that your
   `Kernal`/`Subsystem` already provides (`startup` + `frontier-07 §8`). Reusing it means the
   engine grows without an architectural rewrite — the strongest evidence that your original
   control-plane design was right.

6. **Why is "keep TopK fresh" answered by immutability instead of a mechanism?** Because
   segment-scoped caches can't go stale (the segment can't change) and die on merge; response
   caches invalidate per refresh epoch (`frontier-07 §6`). The hard invalidation problem you
   first imagined was **designed away** by the data model — the deepest systems lesson in both
   series.

---

## 7. The lifetime rule, now generalized (revisit `startup-05 §3` with new eyes)

`startup-05` flagged: in `main.cpp`, `kernal` is declared before the queues, so it's
destroyed *after* them — verify no subsystem thread touches a queue during `~Kernal()`. The
frontier engine **extends** this rule to new resources:

- A **query holding a segment snapshot** (`shared_ptr`, `frontier-03 §8`) must be able to
  outlive a **merge that retires** those segments — that's why retire = *refcount to zero*,
  not *immediate delete*.
- **mmap regions** must stay mapped while any iterator points into them; unmapping under a
  live query is a use-after-free across a syscall boundary.
- The **manifest swap** must be atomic so a reader never sees a half-updated live set.

*Why it's the same rule:* "who outlives whom" governs *all* shared resources — queues,
threads, segments, mmap handles. Master it once (startup), apply it everywhere (frontier).
`shared_ptr` refcounting is your uniform tool for "delete on last use."

---

## 8. The end-state architecture, unified on one page

```
        ┌──────────────── CONTROL PLANE — your Kernal (startup series) ────────────────┐
        │ Register/Init/Boot/Stop · warm-vs-cold decision · flush+commit on cold build  │
        │ owns lifecycle of: pipeline subsystems AND background workers                 │
        └───────────┬───────────────────────────────────────────────┬──────────────────┘
                    │ cold build (once)                              │ steady state
                    ▼                                                ▼
   WRITE PATH (data plane)                              READ PATH (data plane)
   DirReader→Lexer(analyze)→Parser(indexer)             CACHE→analyze→parse→rewrite→
        → in-mem BUFFER  ──FLUSH──► SEGMENT ──COMMIT──►  per-segment(skip+BM25+BlockMaxWAND)
        (poison pill = flush trigger)   │  manifest       →top-K heap→merge→rescore→FETCH
   completion promise → Kernal          │  (atomic swap)  (docID→filepath)
        │                               │
   background workers (Kernal subsystems): Refresh · Flush · Merge · Cache-evict (frontier-07)
                    │                               │
        ┌───────────▼───────────────────────────────▼───────────────────────────────────┐
        │ STORAGE: immutable segments = files of fixed pages (mmap + OS page cache)       │
        │ term dict(B+Tree/FST) · postings(delta/block/skip/impacts) · points/BKD ·       │
        │ doc values · stored fields · norms · manifest/commit point                      │
        └────────────────────────────────────────────────────────────────────────────────┘
```

The **control plane and the queues are your startup/existing code**; the **storage and the
retrieval/ranking mechanisms are the frontier docs**; the **flush/commit seam is where the
two series shake hands.** That handshake is the whole point of this doc.

---

## 9. Final operational self-check

If you can do all of these, you have the operational flow *and* the "why," across both
series:

1. Draw §2 (the unified boot) from memory, and say what happens *between* the completion
   signal and the Engine starting — and why it's there.
2. Explain the `Store` write-side/read-side split and how it dissolves the `startup-01 §7`
   throwaway-tree bug.
3. Trace one cold boot of your `data/` folder: pipeline → buffer → flush → commit → serve.
   Name the artifact on disk at the end.
4. Trace the next (warm) boot: what does `OpenIndex` read, what does "start the Engine" now
   mean, and why is it fast?
5. Place your original "TopK / Trie / B+Tree" trio onto the §4 serve flow, in their *true*
   positions, and justify each placement.
6. Give the master build order (§5) and, for any two adjacent phases, say why the later
   depends on the earlier.
7. State the immutability lesson in one sentence and list three hard problems it designs
   away.

---

## 10. Where this leaves you

You now have **ten documents** that move from "the physics of memory" to "the exact
operational flow of *your* inventory engine, reconciled with your boot design." The path
forward is unglamorous and correct:

- Build **P0 → P2** first (data-plane fix → on-disk segment → warm/cold boot). That yields a
  **persistent, correctly-orchestrated engine** — your startup vision and your first real
  segment, working together.
- Then **P3** (ranked results) — the moment it stops being a toy.
- Then **P4–P7** as depth allows.

When you get stuck, the stuck point is almost always one of two things reasserting itself:
the **memory hierarchy** (doc 01) or **immutability** (doc 03). Return to those two, and the
right design will usually fall out on its own.

The docs are done. The engine — booted your way, indexed the frontier way — is yours to
build, one testable phase at a time.
