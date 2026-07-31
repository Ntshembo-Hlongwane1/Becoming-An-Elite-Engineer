# 03 — Segment Architecture

> The immutability engine that has been quietly explaining everything. This doc makes it
> precise: how documents become **immutable segments**, the exact difference between
> **flush / refresh / commit / merge**, how **deletes** work without mutation (tombstones),
> how a crash is survived (**translog / WAL**), and how readers get a stable **point-in-time**
> view while writers race ahead (**near-real-time**). Covers your items (a) and (h).

---

## 1. The rule, restated as an invariant

> **A segment, once written, is never modified.** Adds create new segments. Deletes mark
> bits. Space is reclaimed only by *rewriting* into a new segment (merge) and dropping the
> old one.

Everything below is a consequence of holding that invariant. Keep asking, as you read: *why
does this design fall out of "never mutate"?* Because that's the reasoning you're here to
learn, not the vocabulary.

---

## 2. The write path: buffer → flush → segment

```
 add(doc) ─► analyze (tokenize/normalize) ─► append to IN-MEMORY buffer
                                                    │
                              buffer full OR refresh timer fires
                                                    ▼
                                          FLUSH: serialize buffer into a NEW segment
                                          (write _N.terms/.postings/.points/.docvalues/.stored)
                                                    ▼
                                          segment _N is now on disk, immutable
```

- Incoming documents are **analyzed** (same tokenizer/normalizer you'll reuse at query
  time — doc 05 §2 makes this symmetry non-negotiable) and accumulated in a RAM buffer
  (Lucene's `IndexWriter` in-memory structures; ES's "indexing buffer").
- When the buffer is full (or a timer fires), it's **flushed**: the buffer is serialized
  into a brand-new segment using all the encodings from docs 02/04. The buffer is then
  cleared.
- The segment is small and self-contained. A busy index accumulates *many* small segments —
  which is why merge (§7) exists.

> **Your engine mapping:** your current cold-start pipeline (DirReader→Lexer→Parser building
> the `unordered_map`) is *exactly* "fill an in-memory buffer." Turning that map into an
> on-disk segment file at the end is your first flush. Incremental adds later = more
> segments.

---

## 3. Flush vs Refresh vs Commit — three words people conflate, kept precise

These are **not** synonyms. They answer three different questions. This section alone
clears up 80% of the confusion around Lucene/ES.

| Operation | Question it answers | Cost | Durable on disk? | Visible to search? |
|---|---|---|---|---|
| **Refresh** | "Make recent docs *searchable*." | cheap (~ms) | not necessarily | **yes** |
| **Flush**\* | "Make recent docs *durable* + free the translog." | expensive (fsync) | **yes** | yes |
| **Commit** | "Record a new *point-in-time* root (`segments_N`), fsync'd." | expensive | **yes** | yes |

\* Terminology clash to be aware of: **ES "flush" ≈ Lucene "commit"** (fsync + clear
translog). Lucene's internal "flush" means "serialize the RAM buffer to a segment." I'll use
**Refresh / Commit** to stay unambiguous, and call the buffer→segment step "flush the
buffer."

The two axes that matter and are **decoupled**:

- **Visibility** (can a search see it?) is provided by **Refresh** — cheap, frequent
  (ES default `refresh_interval: 1s`). Refresh opens a new *reader* over the newly-written
  segment(s), possibly still only in the OS page cache (not fsync'd). This is why ES is
  **Near-Real-Time**: a ~1s lag between "indexed" and "searchable," tunable.
- **Durability** (does it survive a crash?) is provided by **Commit** (fsync the segment
  files + write a new `segments_N`) — expensive, infrequent.

> **The key insight:** *searchable* and *durable* are separated on purpose, because fsync is
> slow and you don't want to pay it on every document. You refresh often (cheap visibility)
> and commit rarely (expensive durability). The gap between them is covered by the translog
> (§5).

---

## 4. The commit point and the atomic switch

A **commit** writes a new `segments_{N+1}` file listing the live segment set, then fsyncs.
Because opening the index means "read the *latest* `segments_N`," the switch from generation
N to N+1 is **atomic**: a reader sees either the old complete set or the new complete set,
never a half-built one.

```
 before commit:  segments_5 → { _0, _1, _2 }         (readers see this)
 (write _3, then write segments_6 → { _0,_1,_2,_3 }, fsync)
 after commit:   segments_6 → { _0, _1, _2, _3 }     (new readers see this)
```

This is the same **atomic-rename / atomic-pointer-swap** idea from your `startup-02` §5
("write tmp, rename over") and from RCU (doc 07). One tiny atomic write flips the whole
index from one consistent state to the next. **Immutability + an atomic root pointer = safe
concurrent evolution with no reader locks.**

---

## 5. Durability between commits: the translog (WAL)

Problem: you refresh every 1s (visible) but commit every 30s (durable). If the process
crashes at second 20, the last 20s of refreshed-but-not-committed docs are only in the OS
page cache — a hard crash/power loss loses them. Unacceptable.

Solution: a **write-ahead log** (ES calls it the **translog**). Every change is *appended*
to the translog and fsync'd (per-request by default, or async for throughput) **before** it
is acknowledged. On restart after a crash, the engine **replays** the translog to
reconstruct anything that was acknowledged but not yet committed into a segment. On a
successful commit, the covered translog prefix is discarded.

```
 write(doc) ─► append to translog (fsync) ─► ack to client
            ─► add to in-memory buffer ─► (refresh makes it searchable)
 ... crash ...
 restart ─► open last commit (segments_N) ─► REPLAY translog tail ─► state restored
```

- The translog is **append-only and sequential** — doc 01 §6: sequential writes are the
  fast kind, so a WAL is cheap to append even when fsync'd.
- This is the **exact** pattern in Postgres (WAL), SQLite, Kafka, and every serious
  datastore: *durability comes from a sequential log; the main structure is updated lazily.*
  It's the write-side twin of "immutable segments."

> **Scope note for you:** a translog is a real feature but *not* required for project-1. Your
> data is batch/rebuildable — a crash mid-index just means re-run the cold build. Know the
> pattern; implement it only when you support live, acknowledged writes.

---

## 6. Deletes and updates without mutation: tombstones

You can't edit an immutable segment, so how do you delete doc #7 from segment `_2`?

- You **don't** touch `_2.postings`. You flip bit 7 in a per-segment **live-docs bitset**
  (Lucene `.liv`). The posting for doc 7 is still physically present; it's **filtered out at
  query time** by AND-ing every result against the live-docs bitset.
- An **update** is a **delete + add**: tombstone the old doc, write the new version into a
  new segment. (This is why "update" is not cheaper than "reindex a doc" in Lucene.)
- The tombstoned data is only *physically* removed when the segment is **merged** (§7) — the
  merge copies live docs into a new segment and skips dead ones.

```
 segment _2 : docs [0..9], liveDocs = 1111111011  (doc 7 tombstoned)
 query match set for _2 = rawMatches AND liveDocs   ← dead docs never surface
 (space for doc 7 reclaimed later, during merge)
```

> **Cost model to internalize:** deletes are *cheap to record* (one bit) but *deferred to
> reclaim* (merge). High-churn workloads accumulate tombstones that bloat segments until
> merged — which is one reason merge scheduling (doc 07) matters.

---

## 7. Merge: how immutable segments stay fast

Many small segments = a query must consult *all* of them (more files, more page faults, more
per-segment overhead). **Merge** consolidates:

```
 pick a set of similar-sized segments ─► stream their LIVE docs into ONE new segment
    _4(1k) _5(1k) _6(1k) _7(1k)  ──merge──►  _8(4k, tombstones purged)
 ─► commit new segments_{N+1} with _8 replacing _4.._7  (atomic swap, §4)
 ─► old segments deleted once no reader references them (ref-count, §8)
```

- Merge runs on **background threads** while old segments keep serving queries — no
  downtime, because the old segments are immutable and still valid until the swap.
- It **purges tombstones** (dead docs aren't copied) — this is where deletes finally free
  space.
- The **merge policy** (Lucene `TieredMergePolicy`) decides *which* segments to merge to
  balance read cost (fewer segments) against write cost (merging rewrites bytes =
  **write amplification**). Details in doc 07.

> **This is an LSM-tree** (doc 01 §6): buffer in RAM → flush sorted immutable runs → merge in
> background. Lucene is, in effect, an LSM-tree of inverted indexes. Same shape as RocksDB,
> Cassandra, etc. Learn it once, recognize it forever.

---

## 8. Point-in-time readers and safe reclamation (the concurrency payoff)

Here's where immutability pays its biggest dividend, and it connects to your `startup-04`
happens-before work.

- A reader (`IndexSearcher`) opens against a **specific commit / segment set** — a
  **point-in-time snapshot**. It sees a *consistent* view even as writers add segments and
  merges run. No read locks, because the segments it holds **cannot change**.
- New searches pick up new data by **reopening** (cheap, incremental — reuse unchanged
  segment readers, open only new ones). This is the NRT reader.
- **Reclamation problem:** a merge wants to delete `_4.._7`, but a slow query might still be
  reading `_5`. Solution: **reference counting** — files/segments are deleted only when the
  last reader referencing them releases (delete-on-last-close). In C++ this is precisely
  what `std::shared_ptr` gives you.

Best-practice sketch of the live-set + snapshot pattern (this is genuinely useful and
generic, not your feature — study the *shape*):

```cpp
// Each segment is immutable and shared. A snapshot is an immutable list of segments.
struct Segment { /* mmap'd readers over its files; never mutated after construction */ };

using SegmentList = std::shared_ptr<const std::vector<std::shared_ptr<const Segment>>>;

class IndexView {
public:
    // Readers grab a snapshot: a shared_ptr to the current immutable segment list.
    // They keep it for the whole query; segments can't vanish under them (refcount).
    SegmentList snapshot() const {
        std::lock_guard lk(mu_);
        return live_;                       // cheap: bumps a refcount
    }

    // Writers/merges publish a NEW immutable list, then swap the pointer (RCU-style).
    void publish(SegmentList next) {
        std::lock_guard lk(mu_);
        live_ = std::move(next);            // old list freed when last reader drops it
    }
private:
    mutable std::mutex mu_;
    SegmentList live_ = std::make_shared<const std::vector<std::shared_ptr<const Segment>>>();
};
```

Why this is the right pattern:
- Readers never block writers and vice-versa (they briefly share a mutex only to
  copy/replace a pointer; the *work* happens lock-free on the snapshot).
- `shared_ptr` refcounting *is* your delete-on-last-close: an old `SegmentList` (and any
  segment unique to it) is destroyed exactly when the last reader holding it finishes.
- It's copy-on-write at the list level: publishing a new set doesn't disturb in-flight
  readers. This is the C++ realization of §4's atomic switch. (Doc 07 revisits this for the
  merge thread; you already used the *idea* — atomic index swap — in your `RingBuffer`.)

> **Advanced note:** the mutex-around-pointer version above is correct and clear. A
> lock-free variant uses `std::atomic<std::shared_ptr<...>>` (C++20) or `atomic_load/store`
> on shared_ptr (pre-C++20). Start with the mutex; it's not on the hot path.

---

## 9. Near-Real-Time, end to end

Put §3–§8 together as the timeline a single document experiences:

```
 t0  add(doc)  → translog append+fsync (durable-in-log) → in-memory buffer (NOT searchable yet)
 t0+≤1s  refresh → buffer flushed to a new segment, new reader opened → SEARCHABLE (NRT lag)
 t0+~30s commit → fsync segments + new segments_N → durable-in-index; translog prefix dropped
 later   merge → segment folded into a bigger one, tombstones purged, old files reclaimed
```

Four different clocks — durability-in-log (immediate), visibility (≤ refresh interval),
durability-in-index (commit interval), compaction (merge policy) — deliberately decoupled so
each can be tuned. That decoupling *is* the architecture.

---

## 10. Mapping to your engine (what to actually build, in order)

1. **Segment = your serialized index file** (doc 02 format). Your cold build → write one
   segment. (You already have the pipeline that fills the buffer.)
2. **Commit = atomic replace** of a tiny `manifest`/`segments` file (write tmp → rename).
   Reuse your `startup-02` atomic-write instinct.
3. **Reopen/point-in-time** = load the manifest → mmap listed segments → serve. A new build
   writes a new segment + new manifest; readers pick it up on reopen.
4. **Multiple segments + merge** = the stretch goal: allow incremental builds to add
   segments, then a background merge (doc 07) to consolidate. This is where your existing
   `Kernal`/`Subsystem`/`RingBuffer` machinery gets reused as the merge scheduler.
5. **Tombstones** = only when you support deletes/updates of inventory items. A live-docs
   bitset per segment, AND-ed into query results.

> **Your turn:** sketch the manifest file. What must it contain so a reader can open the
> index cold? (At minimum: format version, the list of live segment names, per-segment doc
> counts, and checksums — echoing doc 02 §4.) What's the *single* atomic action that flips
> the index from old to new? (Renaming the manifest.) Why does that one atomic action make
> the whole multi-file commit safe?

---

## 11. Before you move on

1. State the immutability invariant and derive *three* other design features from it
   (deletes, merge, lock-free readers).
2. Precisely distinguish **refresh**, **commit**, and "flush the buffer." Which gives
   visibility, which gives durability?
3. What problem does the translog solve, and why is it cheap despite being fsync'd?
4. How is a delete performed without mutating a segment, and when is the space actually
   reclaimed?
5. Why can a query read a segment with zero locks while a merge is rewriting the index?
6. Explain how `std::shared_ptr` implements "delete the old segment files on last reader
   close."

Next: **04 — Posting Lists, Deep**, where we open up a segment's postings file and go to the
bit level: delta encoding, variable-byte, Frame-of-Reference / PForDelta block packing, skip
lists, and the per-block "impacts" that make BlockMax WAND possible — all with worked
byte-by-byte examples.
