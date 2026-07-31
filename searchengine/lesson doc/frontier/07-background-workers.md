# 07 — Background Workers & Scheduling

> The threads that keep the machine healthy while queries and indexing run: the **flush**
> worker (buffer→segment), the **refresh** worker (NRT visibility), the **merge** scheduler +
> workers (consolidation), and **cache eviction**. Plus the **merge policy** and the
> **write-amplification** trade it balances, and how background work stays off the query hot
> path via immutability + atomic swap. The punchline: your existing `Kernal` / `Subsystem` /
> `RingBuffer` / `running_` machinery is *already* the right substrate — this doc shows you
> how to reuse it. Covers your item (g).

---

## 1. Why background workers exist at all

A search engine has two conflicting jobs:

- **Foreground:** answer queries with low, predictable latency.
- **Housekeeping:** persist buffers, make new data visible, consolidate segments, evict caches.

If housekeeping ran on the query path, tail latency would spike whenever a merge or fsync
happened. So housekeeping is pushed to **background threads** that do the slow, bursty work
*without blocking readers* — which is only *safe* because of immutability (doc 03): a merge
builds a new segment beside the old ones, which keep serving until the atomic swap.

```
 foreground: [query] [query] [query] [query] ...        (steady, fast)
 background:      [flush]     [refresh]   [....merge....]   [evict]   (bursty, slow, hidden)
                     └─ never blocks the foreground, thanks to immutable segments + swap
```

---

## 2. The four workers, precisely

| Worker | Trigger | Work | Interacts with |
|---|---|---|---|
| **Flush** | buffer full / timer | serialize in-RAM buffer → new segment (doc 03 §2) | indexing buffer, disk |
| **Refresh** | `refresh_interval` timer | open readers over new segments → NRT visible (doc 03 §3) | reader/searcher view |
| **Merge** | merge policy after segment-set changes | rewrite N segments' live docs → 1 new segment (doc 03 §7) | segment set, disk, CPU |
| **Cache eviction** | cache pressure / segment retirement | drop LRU entries; drop caches tied to retired segments | query/filter caches |

Note the cadences differ by orders of magnitude (refresh ~1s, flush ~seconds, merge
~minutes, eviction on demand) — each worker has its **own clock**, deliberately decoupled
(doc 03 §9). That decoupling is what lets you tune throughput vs latency vs freshness
independently.

---

## 3. A background worker, done right (reuse what you built)

You already have the exact pattern: a subsystem that spawns a thread in `OnStart`, loops
while an atomic flag is set, and joins in `OnStop` (your `DirectoryReader`, `Lexer`,
`Parser`, `Engine`). A background worker is the same shape with a **timer or a work-queue**
instead of a data queue. Best-practice loop:

```cpp
#include <atomic>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <mutex>

// A periodic worker (e.g. Refresh). Sleeps on a condition variable so Stop() wakes it
// immediately instead of waiting out the interval — no busy-wait, prompt shutdown.
class PeriodicWorker {
public:
    PeriodicWorker(std::chrono::milliseconds period) : period_(period) {}

    void start() {
        running_.store(true, std::memory_order_release);
        thread_ = std::thread([this] { run(); });
    }
    void stop() {
        { std::lock_guard lk(mu_); running_.store(false, std::memory_order_release); }
        cv_.notify_all();                          // wake the sleep immediately
        if (thread_.joinable()) thread_.join();    // doc: never destroy a joinable thread
    }
    virtual ~PeriodicWorker() { stop(); }
protected:
    virtual void tick() = 0;                        // the actual housekeeping (e.g. refresh)
private:
    void run() {
        std::unique_lock lk(mu_);
        while (running_.load(std::memory_order_acquire)) {
            // wait up to `period_`, but return early if stop() notifies
            cv_.wait_for(lk, period_, [this] {
                return !running_.load(std::memory_order_acquire);
            });
            if (!running_.load(std::memory_order_acquire)) break;
            lk.unlock();
            tick();                                 // do work WITHOUT holding the lock
            lk.lock();
        }
    }
    std::chrono::milliseconds period_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mutex mu_;
    std::condition_variable cv_;
};
```

Two best-practice points to absorb:

- **Sleep on a CV, not `sleep()`.** `cv_.wait_for(period)` sleeps efficiently *and* wakes
  instantly on `stop()` — so shutdown is prompt (no "wait out the 30s interval"). A raw
  `std::this_thread::sleep_for` can't be interrupted; that's a shutdown-latency bug.
- **Do work outside the lock.** Hold the mutex only to check/flip state; run `tick()`
  unlocked so the worker doesn't serialize with `stop()` or other workers.

> **C++20 upgrade:** `std::jthread` + `std::stop_token` bakes this in — the thread carries a
> cooperative cancellation token and auto-joins on destruction, eliminating the manual
> `running_`/join dance. If your toolchain supports it, prefer `jthread`. Your existing
> `running_` atomic pattern is the pre-C++20 way and is perfectly correct.

A **queue-driven** worker (e.g. the merge worker consuming "merge these segments" tasks) is
your `RingBuffer` + `pop_blocking` pattern verbatim — you already built the primitive.

---

## 4. The merge policy — the central scheduling decision

Merging is not "combine everything." *Which* segments to merge, and when, is a policy that
balances two costs:

- **Read cost** ∝ number of segments (each query consults all live segments). Fewer, bigger
  segments = faster queries.
- **Write cost** = **write amplification**: merging *rewrites* bytes. Merge too eagerly (or
  merge huge segments) and you rewrite the same data many times over its life, burning I/O.

Lucene's **TieredMergePolicy** resolves this:

```
 group segments into size TIERS (roughly exponential: ~small, ~10×, ~100× ...)
 merge segments of SIMILAR size within a tier (cheap: similar-sized inputs)
 cap: max segments-per-tier (bounds read cost) and max merged-segment size (avoids
      re-merging giant segments forever → bounds write amplification)
```

```
 tier 0: [1k][1k][1k][1k][1k][1k][1k][1k][1k][1k]  → merge ten 1k → one 10k
 tier 1: [10k][10k][10k] ...                        → later merge ten 10k → one 100k
 tier 2: [100k] (near max size → left alone; no further re-merge)
```

This keeps the segment count **logarithmic** in document count while ensuring each byte is
rewritten only ~O(log n) times over its lifetime. It's the classic **LSM compaction** trade
(doc 01 §6, doc 03 §7): the same size-tiered idea RocksDB/Cassandra use.

> **The dial:** more aggressive merging → fewer segments → faster reads, but more write I/O
> and CPU. Less aggressive → cheaper writes, but slower reads and slower tombstone
> reclamation. There is no free setting; you pick where on the curve your workload sits. For a
> read-heavy inventory catalog, lean toward fewer segments (favor reads).

---

## 5. Merge without blocking queries — the atomic swap in practice

The merge worker embodies doc 03 §8's snapshot pattern. Concretely:

```
 1. merge worker picks segments {A,B,C} from the live set (a shared_ptr snapshot)
 2. it builds new segment D by streaming A,B,C's LIVE docs (old segments STILL serving queries)
 3. it fsyncs D and writes a new manifest/commit: live set becomes {..., D} minus {A,B,C}
 4. it PUBLISHES the new immutable segment list (atomic pointer/manifest swap)
 5. new queries see {..., D}; in-flight queries keep using their old snapshot of {A,B,C}
 6. A,B,C files deleted when the last reader referencing them releases (shared_ptr refcount)
```

Nothing in steps 1–6 takes a lock that a query waits on for real work. Readers only ever
briefly lock to grab/replace a `shared_ptr` (doc 03 §8 code). **Immutability turns
"modify the index" into "build new + atomically swap + refcount-free the old" — a lock-free
read path even during heavy merging.** This is the single most important reason the frontier
design scales.

---

## 6. Cache eviction — and the freshness question, resolved

Recall your original Q3 ("keep the TopK fresh"). Now you can see the frontier answer in full,
and it's mostly about **eviction tied to immutability**, not surgical updates:

- **Node query cache (filter bitsets, doc 05 §6):** entries are **keyed to a
  (segment, filter)** pair. Since a segment is immutable, the bitset is valid **forever for
  that segment** — no invalidation logic. When the segment is **merged away** (retired), its
  cache entries are simply dropped. Eviction among live entries is **LRU with a size cap**
  (and Lucene only caches "expensive enough" filters on "big enough" segments — caching a
  cheap filter costs more than it saves).
- **Shard request cache (whole responses / aggregations):** **invalidated per refresh** — a
  new segment bumps the effective version, discarding stale entries. That's **epoch
  invalidation** (your earlier option 3D), and it's what frontier engines actually do.
- **The per-query top-K heap (doc 06):** not cached at all — rebuilt each query, cheap thanks
  to WAND.

```
 "keep the cache fresh" in frontier engines =
     immutable segments  → cache entries never go stale (they die with the segment on merge)
   + refresh epochs       → response caches invalidated wholesale on new data
   + LRU + size cap        → bound memory among still-valid entries
   NOT "figure out which cached queries a changed product affects" (that problem is designed away)
```

> **The lesson, one more time:** the hard invalidation problem you anticipated was *avoided by
> the data model*, not solved by clever bookkeeping. Immutability makes "when do I invalidate?"
> answer itself: "when the segment retires." Eviction is then just memory management (LRU), not
> correctness.

Best-practice LRU sketch (correctness-first; the classic hash-map + intrusive list):

```cpp
#include <list>
#include <unordered_map>
#include <optional>

template <class Key, class Val>
class LruCache {
public:
    explicit LruCache(std::size_t cap) : cap_(cap) {}

    std::optional<Val> get(const Key& k) {
        auto it = map_.find(k);
        if (it == map_.end()) return std::nullopt;
        order_.splice(order_.begin(), order_, it->second); // move to front = most-recent
        return it->second->second;
    }
    void put(const Key& k, Val v) {
        if (auto it = map_.find(k); it != map_.end()) {
            it->second->second = std::move(v);
            order_.splice(order_.begin(), order_, it->second);
            return;
        }
        order_.emplace_front(k, std::move(v));
        map_[k] = order_.begin();
        if (map_.size() > cap_) {                          // evict least-recently-used
            map_.erase(order_.back().first);
            order_.pop_back();
        }
    }
    // On segment retirement: erase all entries whose key references that segment.
private:
    std::size_t cap_;
    std::list<std::pair<Key, Val>> order_;                 // front = MRU, back = LRU
    std::unordered_map<Key, typename std::list<std::pair<Key, Val>>::iterator> map_;
};
```

For your engine, the **segment-scoped key** is the trick: make cache keys include the segment
id, and drop a segment's entries when it retires. Correctness comes from the key design, not
from chasing changes.

---

## 7. Backpressure — when housekeeping can't keep up

If indexing outruns merging, segments pile up → read cost climbs → the system degrades. The
fix is **backpressure**: throttle the fast producer when the slow consumer falls behind. ES
literally throttles indexing when too many segments accumulate.

You have a whole doc on this (`backpressure-approach1/2.md`) and the mechanism in your
`RingBuffer` (`push_blocking` blocks when full). The systems mapping:

```
 indexing (produce segments) ──► [segment backlog] ──► merge (consume segments)
     if backlog too deep → BLOCK/slow indexing (push_blocking) until merge catches up
```

Same shape as your Lexer→Parser queue filling up. **Backpressure is how a pipeline stays
stable when stages have different speeds** — and merge-vs-index is just another instance.

---

## 8. It's all your Kernal, reused

Step back: every background worker maps onto machinery you already have.

| Frontier worker | Your existing building block |
|---|---|
| Flush / Refresh / Merge threads | `Subsystem` with `OnStart` spawning a thread, `running_` flag, `OnStop` join |
| Merge task queue | `RingBuffer<MergeTask, N>` + `pop_blocking` |
| Lifecycle ordering (start/stop workers safely) | `Kernal` Register/InitAll/StartAll/StopAll |
| Segment snapshot / atomic swap | `shared_ptr` snapshot (doc 03 §8) — same idea as your atomic ring indices |
| Prompt shutdown of a sleeping worker | `condition_variable::wait_for` + `running_` (this doc §3) |
| Backpressure between index and merge | `push_blocking` when the backlog is full |

You are not learning a foreign architecture — you're learning that **the control-plane/
subsystem/queue/backpressure system you already built is exactly how a search engine
schedules its background work.** Add a `RefreshWorker`, `FlushWorker`, and `MergeScheduler`
as new `Subsystem`s registered with the `Kernal`, and your engine grows into the frontier
shape without a redesign.

> **Your turn:** design a `MergeScheduler` subsystem. What does it observe (segment count/
> sizes)? What does it push onto its task queue (which segments to merge)? Which thread runs
> the actual merge, and how does it publish the new segment set without blocking queries?
> Sketch it against the table above — you already own every primitive it needs.

---

## 9. Before you move on

1. Why can background merging run without blocking queries? Name the two enabling properties.
2. State the read-cost vs write-amplification trade, and how tiered merge balances it.
3. Why sleep a periodic worker on a condition variable instead of `sleep_for`?
4. Explain how cache freshness is achieved by *eviction tied to immutability* rather than
   surgical invalidation — and why segment-scoped cache keys are the trick.
5. Where does backpressure appear between indexing and merging, and which primitive of yours
   implements it?
6. Map each of the four background workers onto a piece of your existing `Kernal`/`Subsystem`/
   `RingBuffer` design.

Next: **08 — Synthesis**, the capstone: the full **write path** and **read path** end to end,
the **index lifecycle**, a staged build roadmap for turning your current engine into this
one, and the honest line between the single-node core you can build and the distributed
ceiling beyond it.
