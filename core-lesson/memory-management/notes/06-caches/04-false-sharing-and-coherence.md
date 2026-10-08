# 6.4 — Cache Coherence and False Sharing

## 1. Coherence: keeping per-core caches agreed

Each core has its own L1/L2 (§6.1). If core 0 and core 1 both cache the same line and core 0 writes
it, core 1's copy must not stay stale. The **cache coherence protocol** (MESI and relatives) enforces
this `[MCCP §2.2]`: a line is in a state like Modified / Exclusive / Shared / Invalid, and a write
forces other cores' copies to **Invalid**, so they re-fetch. Coherence is what makes
`std::atomic`/shared memory work across cores — and it has a cost measured in cache-line transfers
between cores (Lesson 14 builds on this for the memory model).

The granularity of coherence is the **cache line**, not the variable. That one fact causes false
sharing.

## 2. False sharing, measured (~5×)

Put two independent counters that two threads hammer **on the same cache line**, versus padded onto
**separate lines** `[ALGO §22.1.6 note 5]`, `[MCCP §16.2]`. **(measured)** — two threads, 20M
`fetch_add` each:

```
false sharing (same line): 530.3 ms
padded (separate lines):   106.9 ms      (~5x faster)
```

Nothing is logically shared — thread A only touches `a`, thread B only touches `b`. But `a` and `b`
sit in one 64-byte line, so every write by A invalidates B's cached copy of the line and vice-versa;
the line **ping-pongs** between the two cores' caches, each write waiting on a coherence transfer.
Padding each counter to its own line removes the sharing and the ping-pong.

## 3. The fix: one hot datum per line

```cpp
struct Counters {                      // BAD: a and b share a line
    std::atomic<long> a, b;
};
struct Counters {                      // GOOD: each on its own line
    alignas(64) std::atomic<long> a;
    alignas(64) std::atomic<long> b;
};
```

`std::hardware_destructive_interference_size` (64 on your VM, measured in the alignment lesson) is the
standard's name for "pad by at least this to avoid false sharing." This is the write-side mirror of
§6.3's "align hot data to a line." It matters anywhere two threads update nearby fields: per-thread
counters, lock-free queue head/tail (Lesson 14's ring buffer pads exactly these), sharded statistics
(your allocator and your capstone's per-thread metadata).

## 4. True sharing has a cost too
Even correctly-shared data (one counter many threads increment) pays the coherence tax: the line
ping-pongs. The fix there is *algorithmic* — per-thread partials combined at the end (sharding), so
threads touch their own lines and only merge occasionally. "Don't share a line you don't have to" is
the rule for both false and true sharing.

## 5. Where this lands
- Lesson 14 (the C++ memory model & a lock-free ring buffer): you'll pad head/tail to separate lines
  and *measure* the coherence cost of different memory orderings — this section is the hardware under
  that.
- Your capstone's thread-safe metadata (shadow updates, quarantine) must avoid false sharing to scale;
  this is why.

## Drills
1. Reproduce §2. Try the padded version with `alignas(64)` vs a hand `char pad[64]`; confirm both fix
   it. Shrink the pad to 32 bytes — does false sharing partly return? Why 64?
2. Measure true sharing: both threads increment the *same* atomic. Compare to false sharing and to
   per-thread partials summed at the end. Explain the three numbers.
3. Use `perf stat` to watch a coherence-related counter while running the shared vs padded versions.
4. Relate `std::hardware_destructive_interference_size` to the measured 64-byte line.

## My summary
