# The Producer-Consumer Problem & Back Pressure

This document takes the primitives from **backpressure-approach1.md** and applies them to solve your actual problem: data loss between the DirectoryReader and the Lexer.

---

## Table of Contents

1. [The Producer-Consumer Pattern](#1-the-producer-consumer-pattern)
2. [Your Pipeline is Three Producer-Consumer Pairs](#2-your-pipeline-is-three-producer-consumer-pairs)
3. [What Happens When the Producer is Faster](#3-what-happens-when-the-producer-is-faster)
4. [The Three Strategies](#4-the-three-strategies)
5. [Building a Blocking Bounded Buffer — Step by Step](#5-building-a-blocking-bounded-buffer--step-by-step)
6. [Applying This to Your RingBuffer](#6-applying-this-to-your-ringbuffer)
7. [Back Pressure Propagation — The Full Pipeline View](#7-back-pressure-propagation--the-full-pipeline-view)
8. [Clean Shutdown — Replacing Poison Pills](#8-clean-shutdown--replacing-poison-pills)
9. [Testing for Correctness](#9-testing-for-correctness)

---

## 1. The Producer-Consumer Pattern

The producer-consumer pattern is one of the oldest problems in computer science (Dijkstra, 1965). It describes a situation where:

- **Producers** generate work items
- **Consumers** process work items
- A **buffer** sits between them

```
┌──────────┐        ┌─────────┐        ┌──────────┐
│ Producer │ ─────→ │ Buffer  │ ─────→ │ Consumer │
└──────────┘        └─────────┘        └──────────┘
```

The buffer exists because producers and consumers run at **different speeds**. The buffer absorbs temporary speed differences. Without a buffer, the producer would have to wait for the consumer to be ready for every single item — that's called a **synchronous handoff** and it's slow.

### Why a Bounded Buffer?

An **unbounded** buffer (e.g., `std::queue` that grows forever) avoids the "what if it's full?" question entirely. But it introduces a worse problem:

```
Producer speed: 1,000,000 items/sec
Consumer speed: 100,000 items/sec

After 1 second:  900,000 items buffered
After 10 seconds: 9,000,000 items buffered
After 1 minute:  54,000,000 items buffered → out of memory
```

A **bounded** buffer has a fixed capacity. When it's full, the producer can't add more. This forces a decision — and that decision IS back pressure.

Your `RingBuffer<T, 1024>` is a bounded buffer with 1024 slots. The question is: **what does the producer do when all 1024 slots are occupied?**

---

## 2. Your Pipeline is Three Producer-Consumer Pairs

Your search engine has a three-stage pipeline, each connected by a `RingBuffer`:

```
                  dirQueue                lineQueue               parserQueue
                 (1024 slots)            (1024 slots)             (1024 slots)
┌───────────────┐    │    ┌─────────────────┐    │    ┌──────────────────┐    │    ┌────────┐
│ DirectoryReader│───→│───→│ Lexer::Run      │───→│───→│ Lexer::worker x2 │───→│───→│ Parser │
│ (reads files)  │    │    │ (reads lines)   │    │    │ (tokenizes)      │    │    │        │
└───────────────┘    │    └─────────────────┘    │    └──────────────────┘    │    └────────┘
     PRODUCER             CONSUMER/PRODUCER          CONSUMER/PRODUCER          CONSUMER
```

Notice that `Lexer::Run` and `Lexer::worker` are **both** consumers AND producers. They consume from one queue and produce to the next. This is a common pattern called a **pipeline stage**.

Each `RingBuffer<T, 1024>` is a bounded buffer with 1024 slots. Let's analyze the speed mismatch at each stage:

### Stage 1: DirectoryReader → Lexer::Run (via dirQueue)

- **Producer**: `DirectoryReader::Run()` iterates over `data/` and pushes file paths
- **Consumer**: `Lexer::Run()` pops file paths and opens each file
- **Speed mismatch**: Tiny. The DirectoryReader only pushes as many items as there are files in the directory. Unless you have 1024+ files, this queue won't overflow.
- **Risk**: Low (but not zero — the fix is still worth applying)

### Stage 2: Lexer::Run → Lexer::worker (via lineQueue)

- **Producer**: `Lexer::Run()` reads lines with `fgets()` and pushes `ILP` structs
- **Consumer**: Two `Lexer::worker` threads pop lines and tokenize them
- **Speed mismatch**: **HUGE.** `fgets()` is essentially a `memcpy` from a kernel buffer — it can read tens of thousands of lines per second. Each worker has to:
  1. Pop a line
  2. Split it into tokens (`splitLine`)
  3. Strip punctuation from each token
  4. Check each token against the stop words set
  5. Push each non-stop-word token to `parserQueue`
  
  Steps 2-5 are much slower than step 1 of the producer (`fgets`). A single data file with 2000 lines will overflow the 1024-slot buffer.
- **Risk**: **CRITICAL** — this is where your data loss is happening

### Stage 3: Lexer::worker → Parser (via parserQueue)

- **Producer**: Two `Lexer::worker` threads push individual tokens
- **Consumer**: `Parser::Run()` pops tokens and processes them
- **Speed mismatch**: Depends on what the parser does. If the parser is building an inverted index (hash map insertions), it might be slower than the tokenizer.
- **Risk**: Moderate

---

## 3. What Happens When the Producer is Faster

Let's trace exactly what happens in your code when `lineQueue` fills up.

Your ring buffer's `push` method (`ringbuffer.hpp L15-26`):

```cpp
bool push(const T& item){
    size_t write = write_index.load(std::memory_order_relaxed);
    size_t read = read_index.load(std::memory_order_acquire);

    if ((write - read) >= SIZE){
        return false;           // ← BUFFER FULL: returns false
    };

    slots[write & (SIZE - 1)] = item;
    write_index.store(write + 1, std::memory_order_release);
    return true;
};
```

Your producer in `lexer.cpp L141-147`:

```cpp
{
    std::lock_guard<std::mutex> lock(lineQueueMutex);
    lineQueue.push({ file, std::string(line) });   // ← return value ignored!
}
queueCV.notify_one();
```

### The Timeline of Data Loss

```
Time    write_index    read_index    Buffer Used    push() returns
────    ───────────    ──────────    ───────────    ──────────────
  0          0              0           0/1024      true  ← "Redis is an in-memory database."
  1          1              0           1/1024      true  ← "Redis supports caching."
  2          2              0           2/1024      true  ← "Kafka is an event streaming..."
  ...
 50         50              0          50/1024      true  ← workers haven't started popping yet
  ...
500        500              0         500/1024      true  ← workers popped 0 (busy tokenizing)
  ...
1023      1023              0        1023/1024      true  ← last slot
1024      1024              0        1024/1024      FALSE ← BUFFER FULL
1025      1024              0        1024/1024      FALSE ← LINE LOST: "Go supports concurrency..."
1026      1024              0        1024/1024      FALSE ← LINE LOST: "Goroutines are lightweight..."
1027      1024              1        1023/1024      true  ← worker finally popped one item
```

Look at lines 1024-1026: `push()` returns `false`, but your code **doesn't check the return value.** The line is simply never stored. `write_index` stays at 1024 because the `if` check prevented the store.

**Those lines are gone forever.** Your search index will be incomplete. If someone searches for "goroutines" and that line was dropped, they get no results.

### Quantifying the Loss

How many lines get dropped? It depends on the speed ratio:

```
Producer speed: ~5,000,000 lines/sec (fgets from kernel buffer)
Consumer speed: ~500,000 lines/sec (tokenize + strip + stopword check, per worker)
Effective consumer speed (2 workers): ~1,000,000 lines/sec

Speed ratio: 5:1

For a 5000-line file:
  - Buffer fills after 1024 lines
  - Remaining 3976 lines: producer pushes at 5M/s, consumers drain at 1M/s
  - Net overflow rate: 4M/s
  - Time to read remaining lines: ~0.8ms
  - Items lost in 0.8ms at 4M/s net overflow: ~3200 lines

  Approximately 3200 of 5000 lines lost. That's 64% of the document.
```

This is an estimate, but the magnitude is real. You will lose a significant fraction of large documents.

---

## 4. The Three Strategies

When the buffer is full, you have exactly three choices:

### Strategy 1: Drop (What You're Doing Now)

```cpp
if (!buffer.push(item)) {
    // item is lost. tough luck.
}
```

**When this is acceptable**: Metrics/telemetry (losing 1% of data points is fine), audio/video streaming (dropping a frame is better than stuttering), logging (better to lose a log line than slow down the application).

**When this is NOT acceptable**: Your search engine. Every dropped line means a document is partially indexed. Search results become wrong. If someone searches for a word that only appeared in a dropped line, they get no results. This is a correctness bug, not a performance issue.

### Strategy 2: Spin (Busy Wait)

```cpp
while (!buffer.push(item)) {
    // keep trying until space opens up
    std::this_thread::yield();
}
```

The producer burns CPU cycles checking the buffer in a tight loop. `yield()` tells the OS "I have nothing useful to do, give my time slice to another thread" — but the thread is still runnable and will be scheduled again quickly.

**The irony of spinning**: Spinning can actually make things **worse**. The producer thread is burning CPU cycles that the consumer threads need. On a 4-core machine:

```
Core 0: Producer (spinning — consuming 100% CPU doing nothing useful)
Core 1: Worker 1 (tokenizing — needs CPU)
Core 2: Worker 2 (tokenizing — needs CPU)  
Core 3: Parser (processing — needs CPU)
```

If you only have 2 cores:

```
Core 0: Producer (spinning) + Worker 1 (time-sliced, gets less CPU)
Core 1: Worker 2 + Parser (time-sliced)
```

The spinning producer is stealing time from the worker that needs to drain the buffer! This is called **priority inversion** — the thing causing the problem is getting more resources than the thing trying to fix it.

**Pros**: Simple. No new primitives needed.
**Cons**: Wastes CPU. Can make the problem worse via priority inversion.

### Strategy 3: Block (Sleep Until Space is Available)

```cpp
void push_blocking(const T& item) {
    std::unique_lock<std::mutex> lock(mtx);
    
    not_full_cv.wait(lock, [&] { return !buffer.full(); });
    
    buffer.push(item);
    
    lock.unlock();
    not_empty_cv.notify_one();
}
```

The producer **goes to sleep**. The OS removes it from the CPU's run queue — it uses **zero** CPU cycles. It doesn't appear in `top` or Task Manager as consuming CPU. When the consumer pops an item, it wakes the producer up. The producer pushes its item and continues.

```
Core 0: (idle — producer is sleeping)       ← CPU available for OS/other work
Core 1: Worker 1 (tokenizing — full CPU)
Core 2: Worker 2 (tokenizing — full CPU)
Core 3: Parser (processing)
```

When worker 1 pops an item → calls `not_full.notify_one()` → producer wakes up on Core 0 → pushes one item → goes back to sleep if buffer is still full.

**The producer runs at exactly the speed the consumer can handle.** No faster, no slower. No wasted CPU. No lost data.

**Pros**: Zero wasted CPU. True back pressure. No data loss.
**Cons**: More complex code. Slight latency from sleeping/waking (~1-10 microseconds).

> **This is the correct choice for your search engine.** You can't lose data (Strategy 1 is out). You don't want to burn CPU (Strategy 2 is wasteful). Blocking is the standard solution for bounded producer-consumer buffers in every production system.

---

## 5. Building a Blocking Bounded Buffer — Step by Step

Let's build this from scratch so you understand every line. We'll start with the simplest possible version and add complexity only when needed.

### Step 1: The Unbounded Case (No Buffer Limit)

First, let's solve a simpler problem: a consumer that blocks when the queue is empty. No back pressure yet — just "don't busy-wait when there's nothing to consume."

```cpp
#include <queue>
#include <mutex>
#include <condition_variable>

template<typename T>
class UnboundedQueue {
    std::queue<T> items;
    std::mutex mtx;
    std::condition_variable not_empty;

public:
    void push(const T& item) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            items.push(item);
        }
        not_empty.notify_one();     // wake a consumer
    }

    T pop() {
        std::unique_lock<std::mutex> lock(mtx);
        not_empty.wait(lock, [&] { return !items.empty(); });
        T item = items.front();
        items.pop();
        return item;
    }
};
```

**What this solves**: Consumers don't spin when the queue is empty. They sleep and get woken up when there's work.

**What this doesn't solve**: There's no limit on queue size. If the producer pushes 10 million items and the consumer processes 1 million, you have 9 million items in memory. On a machine with 8GB RAM, each `ILP` struct (~64 bytes for two `std::string`s), that's ~576MB. With enough data, you'll run out of memory.

### Step 2: Add a Size Limit (But No Blocking Push)

```cpp
template<typename T>
class BoundedQueue {
    std::queue<T> items;
    std::mutex mtx;
    std::condition_variable not_empty;
    size_t max_size;

public:
    BoundedQueue(size_t max) : max_size(max) {}

    bool push(const T& item) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            if (items.size() >= max_size) {
                return false;           // ← same problem as your RingBuffer
            }
            items.push(item);
        }
        not_empty.notify_one();
        return true;
    }

    T pop() {
        std::unique_lock<std::mutex> lock(mtx);
        not_empty.wait(lock, [&] { return !items.empty(); });
        T item = items.front();
        items.pop();
        return item;
    }
};
```

This is essentially what you have now, just with `std::queue` instead of a ring buffer array. The `push` still fails when full. The caller has to deal with it. Nobody in your code deals with it.

### Step 3: Make Push Block (This is Back Pressure)

The key insight: **add a second condition variable for "not full."**

We already have `not_empty` — consumers wait on it when the queue has no data. Now add `not_full` — producers wait on it when the queue has no space.

```cpp
template<typename T>
class BlockingBoundedQueue {
    std::queue<T> items;
    std::mutex mtx;
    std::condition_variable not_empty;   // consumers wait on this
    std::condition_variable not_full;    // producers wait on this  ← NEW
    size_t max_size;

public:
    BlockingBoundedQueue(size_t max) : max_size(max) {}

    void push(const T& item) {                          // ← returns void, cannot fail
        std::unique_lock<std::mutex> lock(mtx);         // ← unique_lock, not lock_guard
        
        // BACK PRESSURE: if the buffer is full, SLEEP until there's room
        not_full.wait(lock, [&] { 
            return items.size() < max_size; 
        });
        
        items.push(item);
        
        lock.unlock();
        not_empty.notify_one();     // wake a consumer — there's data now
    }

    T pop() {
        std::unique_lock<std::mutex> lock(mtx);
        
        // If the buffer is empty, SLEEP until there's data
        not_empty.wait(lock, [&] { 
            return !items.empty(); 
        });
        
        T item = items.front();
        items.pop();
        
        lock.unlock();
        not_full.notify_one();      // wake a producer — there's space now  ← NEW
        
        return item;
    }
};
```

**This is the complete solution to back pressure.** Let's trace what happens in detail:

### Trace: push() When Buffer is Full

```
1. Producer calls push(item)
2. unique_lock<mutex> lock(mtx)          → acquires the lock
3. not_full.wait(lock, predicate)
   3a. Check predicate: items.size() < max_size?
       items.size() == 1024, max_size == 1024
       1024 < 1024 → FALSE
   3b. Predicate is false → unlock mtx, go to sleep
       (producer is now sleeping, using 0 CPU)
       (other threads can acquire mtx because we unlocked it)
4. ... time passes ... consumer calls pop() ...
5. Consumer's pop() calls not_full.notify_one()
6. Producer wakes up
   6a. Re-lock mtx
   6b. Check predicate again: items.size() < max_size?
       items.size() == 1023 (consumer popped one)
       1023 < 1024 → TRUE
   6c. Predicate is true → return from wait()
7. items.push(item)                       → push the item
8. lock.unlock()
9. not_empty.notify_one()                → wake a consumer
10. return
```

### Trace: pop() When Buffer is Empty

```
1. Consumer calls pop()
2. unique_lock<mutex> lock(mtx)          → acquires the lock
3. not_empty.wait(lock, predicate)
   3a. Check predicate: !items.empty()?
       items is empty → FALSE
   3b. Predicate is false → unlock mtx, go to sleep
4. ... time passes ... producer calls push() ...
5. Producer's push() calls not_empty.notify_one()
6. Consumer wakes up
   6a. Re-lock mtx
   6b. Check predicate again: !items.empty()?
       items has 1 item → TRUE
   6c. Predicate is true → return from wait()
7. item = items.front(); items.pop()     → grab the item
8. lock.unlock()
9. not_full.notify_one()                → wake a producer
10. return item
```

### The Symmetry

```
               ┌─── push blocks here ───┐
               │                        │
Producer ────→ not_full.wait()     not_empty.notify()
                                        │
                                        ↓
Consumer ←──── not_empty.wait()    not_full.notify() ←── pop wakes producer
               │                        │
               └─── pop blocks here ────┘
```

Each side waits on one CV and signals the other. This is the classic **two-condition-variable bounded buffer**.

### Why Two Condition Variables, Not One?

You might think: "Can't I use a single condition variable for both cases?"

```cpp
// PROBLEMATIC: single CV
std::condition_variable cv;

void push(const T& item) {
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [&] { return items.size() < max_size; });
    items.push(item);
    lock.unlock();
    cv.notify_one();    // ← who gets woken up?
}

T pop() {
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [&] { return !items.empty(); });
    T item = items.front();
    items.pop();
    lock.unlock();
    cv.notify_one();    // ← who gets woken up?
    return item;
}
```

The problem:

```
Scenario: buffer has 1 slot, is currently full.
  - Consumer 1 is waiting (buffer empty earlier, not yet woken)
  - Producer 1 is waiting (buffer full)
  - Producer 2 is waiting (buffer full)

Consumer 2 calls pop():
  - Pops an item
  - Calls cv.notify_one()
  - Wakes up... Consumer 1!  ← WRONG! Consumer 1 has nothing to consume
  - Consumer 1 checks predicate → buffer is empty → goes back to sleep
  - Producer 1 and Producer 2 are still sleeping, nobody wakes them
  - DEADLOCK: producers are stuck, consumers are stuck
```

With **two CVs**, `pop()` calls `not_full.notify_one()`, which can **only** wake a producer. `push()` calls `not_empty.notify_one()`, which can **only** wake a consumer. The right type of thread always gets woken.

You *could* use one CV with `notify_all()` instead of `notify_one()`, but that's wasteful — every sleeping thread wakes up, checks its predicate, and most go right back to sleep. With N sleeping threads, that's N-1 wasted context switches. Two CVs are the standard approach.

---

## 6. Applying This to Your RingBuffer

You already have a high-performance lock-free `RingBuffer`. You don't need to throw it away. Instead, **add blocking wrappers alongside the existing non-blocking methods.**

The non-blocking `push()` and `pop()` are still useful for hot paths where you know the buffer won't be full/empty. The blocking versions are for the general case.

### What to Add to ringbuffer.hpp

```cpp
#pragma once
#include <atomic>
#include <cstddef>
#include <mutex>                    // NEW
#include <condition_variable>       // NEW

template<typename T, size_t SIZE>
class RingBuffer{
    private:
        static_assert((SIZE & (SIZE - 1)) == 0, "SIZE must be power of 2");
        alignas(64) std::atomic<size_t> write_index{0};
        alignas(64) std::atomic<size_t> read_index{0};
        alignas(64) T slots[SIZE];

        // NEW: synchronization for blocking operations
        std::mutex mtx;
        std::condition_variable not_full;
        std::condition_variable not_empty;

    public:
        // ── Existing non-blocking API (unchanged) ──────────────

        bool push(const T& item){
            size_t write = write_index.load(std::memory_order_relaxed);
            size_t read = read_index.load(std::memory_order_acquire);

            if ((write - read) >= SIZE){
                return false;
            };

            slots[write & (SIZE - 1)] = item;
            write_index.store(write + 1, std::memory_order_release);
            return true;
        };

        bool pop(T& item){
            size_t read = read_index.load(std::memory_order_relaxed);
            size_t write = write_index.load(std::memory_order_acquire);

            if (read >= write){
                return false;
            };

            item = slots[read & (SIZE - 1)];

            read_index.store(read + 1, std::memory_order_release);
            return true;
        };

        
        bool empty() const {
            size_t read = read_index.load(std::memory_order_acquire);
            size_t write = write_index.load(std::memory_order_acquire);
            return read >= write;
        }

        // ── NEW: Blocking API ──────────────────────────────────

        // Blocks until there is space, then pushes the item.
        void push_blocking(const T& item) {
            std::unique_lock<std::mutex> lock(mtx);

            // Wait until there's at least one free slot
            not_full.wait(lock, [this] {
                size_t w = write_index.load(std::memory_order_relaxed);
                size_t r = read_index.load(std::memory_order_acquire);
                return (w - r) < SIZE;
            });

            // Space is available — do the actual push
            size_t write = write_index.load(std::memory_order_relaxed);
            slots[write & (SIZE - 1)] = item;
            write_index.store(write + 1, std::memory_order_release);

            lock.unlock();
            not_empty.notify_one();  // wake a consumer
        }

        // Blocks until there is data, then pops the item.
        void pop_blocking(T& item) {
            std::unique_lock<std::mutex> lock(mtx);

            // Wait until there's at least one item
            not_empty.wait(lock, [this] {
                size_t r = read_index.load(std::memory_order_relaxed);
                size_t w = write_index.load(std::memory_order_acquire);
                return r < w;
            });

            // Data is available — do the actual pop
            size_t read = read_index.load(std::memory_order_relaxed);
            item = slots[read & (SIZE - 1)];
            read_index.store(read + 1, std::memory_order_release);

            lock.unlock();
            not_full.notify_one();  // wake a producer
        }

        // NEW: check if buffer is full
        bool full() const {
            size_t w = write_index.load(std::memory_order_acquire);
            size_t r = read_index.load(std::memory_order_acquire);
            return (w - r) >= SIZE;
        }
};
```

### Why This Design Works

The blocking methods use the mutex + condition variable pattern from Step 3, but the actual data manipulation still uses the same atomic operations as the lock-free versions. The mutex here is **not protecting the data** (the atomics already do that) — it's **protecting the sleep/wake protocol** of the condition variable.

```
push_blocking():
  1. Lock mutex                           ← protects the CV wait
  2. Wait until not full                  ← sleeps if full
  3. Push item (using atomics)            ← same as non-blocking push
  4. Unlock mutex
  5. Notify not_empty                     ← wake consumer

pop_blocking():
  1. Lock mutex                           ← protects the CV wait
  2. Wait until not empty                 ← sleeps if empty
  3. Pop item (using atomics)             ← same as non-blocking pop
  4. Unlock mutex
  5. Notify not_full                      ← wake producer
```

### What to Change in Your Producers

**directoryreader.cpp — line 33:**

```diff
 for (const auto& entry : std::filesystem::directory_iterator(dataPath)){
     if (entry.is_regular_file()){
-        dirQueue.push(entry.path().string());
+        dirQueue.push_blocking(entry.path().string());
     }
 }
```

**lexer.cpp — lines 141-147** (the lineQueue producer):

```diff
-{
-    std::lock_guard<std::mutex> lock(lineQueueMutex);
-    lineQueue.push({ file, std::string(line) }); 
-}
-queueCV.notify_one();
+lineQueue.push_blocking({ file, std::string(line) });
```

Notice how `push_blocking` replaces the **entire manual mutex + push + notify pattern**. The ring buffer now handles its own synchronization internally. This is cleaner AND correct.

**lexer.cpp — line 97** (the parserQueue producer):

```diff
-{
-    std::lock_guard<std::mutex> lock(g_parserMutex);
-    parserQueue.push(token);
-}
+parserQueue.push_blocking(token);
```

### What to Change in Your Consumers

**lexer.cpp — the worker loop at lines 60-77:**

This is the biggest simplification. Your current worker has 15 lines of manual synchronization:

```cpp
// CURRENT: 15 lines of manual sync
while(running.load(std::memory_order_acquire)){
    ILP line;

   { 
        std::unique_lock<std::mutex> lock(lineQueueMutex);

        queueCV.wait(lock, [this] { 
            return !lineQueue.empty() || !running.load(std::memory_order_acquire); 
        });

        if (!running.load(std::memory_order_acquire) && lineQueue.empty()) {
            break;
        }

        if (!lineQueue.pop(line)){
            continue;
        }
    }
    
    // ... process line ...
}
```

With `pop_blocking`, this collapses to:

```cpp
// NEW: 4 lines
while(running.load(std::memory_order_acquire)){
    ILP line;
    lineQueue.pop_blocking(line);
    
    // ... process line ...
}
```

The entire `unique_lock` + `wait` + `if empty break` + `if !pop continue` block is gone. The ring buffer's `pop_blocking` handles all of it internally.

### What You Can Remove from lexer.hpp

After this change, these members are no longer needed:

```diff
-    mutable std::mutex lineQueueMutex;
-    std::condition_variable queueCV;
```

The ring buffer now owns its own synchronization. You can also remove the `#include <condition_variable>` and `#include <thread>` from `lexer.hpp` if nothing else uses them.

---

## 7. Back Pressure Propagation — The Full Pipeline View

Here's the most powerful property of this design: **back pressure propagates automatically upstream through the entire pipeline.**

### Scenario: The Parser is Slow

Imagine the parser takes 10ms to process each token. Here's what happens:

```
Time 0ms:
  DirectoryReader: pushing file paths to dirQueue
  Lexer::Run: reading lines, pushing to lineQueue
  Worker 1: tokenizing, pushing to parserQueue
  Worker 2: tokenizing, pushing to parserQueue
  Parser: processing tokens (slowly, 10ms each)
  
Time 50ms:
  parserQueue fills up (1024 tokens, parser processed only 5)
  
Time 51ms:
  Worker 1 calls parserQueue.push_blocking(token)
  → parserQueue is full
  → Worker 1 SLEEPS
  
Time 52ms:
  Worker 2 calls parserQueue.push_blocking(token)
  → parserQueue is still full
  → Worker 2 SLEEPS
  
Time 53ms:
  Both workers are sleeping
  → Nobody is popping from lineQueue
  → Lexer::Run keeps pushing lines to lineQueue
  
Time 100ms:
  lineQueue fills up (1024 lines, nobody popping)
  
Time 101ms:
  Lexer::Run calls lineQueue.push_blocking({file, line})
  → lineQueue is full
  → Lexer::Run SLEEPS
  
Time 102ms:
  Lexer::Run is sleeping
  → Nobody is popping from dirQueue
  → DirectoryReader keeps pushing file paths
  
Time 200ms (if enough files):
  dirQueue fills up (1024 paths, nobody popping)
  
Time 201ms:
  DirectoryReader calls dirQueue.push_blocking(path)
  → dirQueue is full
  → DirectoryReader SLEEPS
```

**The entire pipeline is now sleeping except the parser.** Every thread is using 0% CPU. The pipeline has automatically throttled to the speed of the slowest stage.

### The Wake-Up Cascade

When the parser finally processes a token:

```
Time 210ms:
  Parser calls parserQueue.pop_blocking(token)
  → not_full.notify_one()
  → Worker 1 wakes up
  
Time 210.01ms:
  Worker 1 pushes its token to parserQueue
  Worker 1 calls lineQueue.pop_blocking(line)
  → not_full.notify_one()
  → Lexer::Run wakes up
  
Time 210.02ms:
  Lexer::Run pushes its line to lineQueue
  Lexer::Run reads next line from file with fgets()
  Lexer::Run calls lineQueue.push_blocking({file, line})
  → if lineQueue is full again, sleeps again
  → if not, pushes and calls dirQueue.pop_blocking()
  → not_full.notify_one()
  → DirectoryReader wakes up (if sleeping)
```

The "wake up" ripples upstream, one stage at a time. Each stage does one unit of work and may go back to sleep if its downstream is still full.

### Visualizing the Steady State

After the initial burst, the pipeline reaches a **steady state** where each stage runs at the speed of the slowest:

```
Parser processes 1 token
  └→ Worker pops 1 line, produces N tokens, pushes 1 (blocks on the rest)
     └→ Lexer::Run pops 0 or 1 file, reads 1 line, pushes 1 (blocks if full)
        └→ DirectoryReader pushes 0 or 1 file (blocks if full)
```

The throughput of the entire pipeline equals the throughput of the parser. This is exactly the correct behavior — you can't go faster than your slowest stage, and going faster than that just wastes memory and CPU.

> **This is exactly how systems like Kafka, Go channels, and Unix pipes work.** A shell pipeline like `cat bigfile | grep pattern | sort` has the same back pressure behavior — `cat` blocks on write when `grep`'s stdin buffer is full, and `grep` blocks when `sort`'s stdin buffer is full.

---

## 8. Clean Shutdown — Replacing Poison Pills

Your current code uses **poison pills** (empty strings) to signal shutdown at `lexer.cpp L115-117`:

```cpp
if (file == ""){
    std::cout << "\n [" << Name() << "] POISON PILL. Exiting" << std::endl;
    break;
}
```

This works, but it has problems with blocking queues:

### Problem 1: Blocked Thread Never Sees the Poison Pill

```
Scenario: Worker 1 is stuck in pop_blocking() (lineQueue is empty)
You push a poison pill to lineQueue.
Worker 1 wakes up, gets the poison pill, exits.
But Worker 2 is also stuck in pop_blocking(). There's only one poison pill.
Worker 2 sleeps forever.
```

You need to push **one poison pill per worker thread**. If you have 2 workers, push 2 pills. If you later add a 3rd worker, you need to remember to push 3. This is fragile.

### Problem 2: Blocked Producer Never Gets the Pill

```
Scenario: Lexer::Run is stuck in push_blocking() (lineQueue is full)
DirectoryReader pushes a poison pill to dirQueue.
But Lexer::Run is not calling dirQueue.pop() — it's blocked on lineQueue.push_blocking().
The poison pill sits in dirQueue, never consumed.
```

### A Cleaner Solution: close()

Add a `close()` method to the ring buffer that wakes all sleeping threads:

```cpp
template<typename T, size_t SIZE>
class RingBuffer {
    // ... existing members ...
    std::atomic<bool> closed{false};    // NEW

public:
    // NEW: close the buffer — wakes all sleeping threads
    void close() {
        closed.store(true, std::memory_order_release);
        not_full.notify_all();    // wake all sleeping producers
        not_empty.notify_all();   // wake all sleeping consumers
    }

    bool is_closed() const {
        return closed.load(std::memory_order_acquire);
    }

    // MODIFIED: push_blocking now returns false if closed
    bool push_blocking(const T& item) {
        std::unique_lock<std::mutex> lock(mtx);

        not_full.wait(lock, [this] {
            size_t w = write_index.load(std::memory_order_relaxed);
            size_t r = read_index.load(std::memory_order_acquire);
            return (w - r) < SIZE || closed.load(std::memory_order_acquire);
            //                       ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
            //                       Wake up even if full, so we can exit
        });

        // If channel was closed, don't push — just exit
        if (closed.load(std::memory_order_acquire)) {
            return false;
        }

        size_t write = write_index.load(std::memory_order_relaxed);
        slots[write & (SIZE - 1)] = item;
        write_index.store(write + 1, std::memory_order_release);

        lock.unlock();
        not_empty.notify_one();
        return true;
    }

    // MODIFIED: pop_blocking now returns false if closed AND empty
    bool pop_blocking(T& item) {
        std::unique_lock<std::mutex> lock(mtx);

        not_empty.wait(lock, [this] {
            size_t r = read_index.load(std::memory_order_relaxed);
            size_t w = write_index.load(std::memory_order_acquire);
            return r < w || closed.load(std::memory_order_acquire);
        });

        // If closed AND empty, signal "no more data will ever come"
        size_t read = read_index.load(std::memory_order_relaxed);
        size_t write = write_index.load(std::memory_order_acquire);
        if (read >= write && closed.load(std::memory_order_acquire)) {
            return false;   // nothing left, channel is done
        }

        // Data is available — pop it
        item = slots[read & (SIZE - 1)];
        read_index.store(read + 1, std::memory_order_release);

        lock.unlock();
        not_full.notify_one();
        return true;
    }
};
```

### The Key Semantics of close()

`close()` does NOT discard remaining data. This is critical:

```
After close():
  - push_blocking() returns false immediately (no more items accepted)
  - pop_blocking() keeps returning true until the buffer is EMPTY
  - Once both closed AND empty, pop_blocking() returns false
  
  All items pushed before close() will be consumed. No data loss.
```

This is the same semantic as Go's `close(channel)`:
```go
close(ch)       // no more sends
for item := range ch {  // keeps receiving until channel is drained
    process(item)
}
// loop exits when channel is closed AND empty
```

### How Each Stage Uses close()

```cpp
// DirectoryReader::Run — after iterating all files
void DirectoryReader::Run() {
    for (const auto& entry : std::filesystem::directory_iterator(dataPath)){
        if (entry.is_regular_file()){
            dirQueue.push_blocking(entry.path().string());
        }
    }
    dirQueue.close();   // "I have no more files. Drain what's left."
}

// Lexer::Run — drain until dirQueue is closed and empty
void Lexer::Run() {
    std::string file;
    while (dirQueue.pop_blocking(file)) {   // returns false when closed + empty
        FILE* fp = fopen(file.c_str(), "r");
        if (!fp) continue;
        
        char line[456];
        while(fgets(line, sizeof(line), fp) != nullptr){
            // ... strip newline ...
            lineQueue.push_blocking({ file, std::string(line) });
        }
        fclose(fp);
    }
    lineQueue.close();  // "I have no more lines. Drain what's left."
}

// Lexer::worker — drain until lineQueue is closed and empty
void Lexer::worker(std::string thread) {
    ILP line;
    while (lineQueue.pop_blocking(line)) {  // returns false when closed + empty
        auto tokens = splitLine(line.line);
        for (auto& token : tokens) {
            // ... strip punctuation, check stop words ...
            parserQueue.push_blocking(token);
        }
    }
    // Note: only the LAST worker to exit should close parserQueue
    // This requires coordination — see below
}
```

### Coordinating Multiple Workers at Shutdown

With two worker threads, you need to ensure `parserQueue.close()` is called only **after both workers have finished**. If Worker 1 exits and closes the queue while Worker 2 is still pushing tokens, Worker 2's `push_blocking` will return `false` and tokens will be lost.

Use an `atomic<int>` to count active workers:

```cpp
// In Lexer class
std::atomic<int> active_workers{2};

void Lexer::worker(std::string thread) {
    ILP line;
    while (lineQueue.pop_blocking(line)) {
        // ... tokenize and push to parserQueue ...
    }
    
    // This worker is done. Decrement the count.
    int remaining = active_workers.fetch_sub(1, std::memory_order_acq_rel);
    if (remaining == 1) {
        // I was the last worker — safe to close the downstream queue
        parserQueue.close();
    }
}
```

`fetch_sub` atomically subtracts 1 and returns the **previous** value. If it returns 1, the previous value was 1, meaning this thread decremented it to 0 — it's the last worker.

---

## 9. Testing for Correctness

Once you implement this, here's how to verify it works:

### Test 1: Count Everything

Add atomic counters at each stage boundary:

```cpp
std::atomic<int> files_pushed{0};
std::atomic<int> files_popped{0};
std::atomic<int> lines_pushed{0};
std::atomic<int> lines_popped{0};
std::atomic<int> tokens_pushed{0};
std::atomic<int> tokens_popped{0};
```

Increment them at each push/pop site. At the end of the pipeline, print and assert:

```cpp
std::cout << "Files:  pushed=" << files_pushed << " popped=" << files_popped << std::endl;
std::cout << "Lines:  pushed=" << lines_pushed << " popped=" << lines_popped << std::endl;
std::cout << "Tokens: pushed=" << tokens_pushed << " popped=" << tokens_popped << std::endl;

assert(files_pushed == files_popped);
assert(lines_pushed == lines_popped);
assert(tokens_pushed == tokens_popped);
```

If any of these fail, you're still losing data somewhere.

### Test 2: Tiny Buffer, Big Data

Temporarily reduce the ring buffer to a very small size:

```cpp
RingBuffer<std::string, 4> dirQueue;      // only 4 slots!
RingBuffer<ILP, 4> lineQueue;             // only 4 slots!
RingBuffer<std::string, 4> parserQueue;   // only 4 slots!
```

Now run with your normal data set. If back pressure works correctly:
- Every file will still be processed
- Every line will still be tokenized
- Every token will still reach the parser
- It will just be **slower** because the producer blocks constantly

If you're dropping data, you'll see mismatched counts from Test 1. A buffer of 4 will expose any remaining "unchecked push" bugs immediately.

### Test 3: Slow Consumer

Add an artificial delay in the worker to simulate a slow consumer:

```cpp
void Lexer::worker(std::string thread) {
    ILP line;
    while (lineQueue.pop_blocking(line)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));  // ← artificial delay
        // ... process ...
    }
}
```

Observe:
- **CPU usage**: Should be low. The producer should be sleeping most of the time, not spinning. If you see 100% CPU on the producer thread, your back pressure isn't working.
- **Data integrity**: All lines should still be processed. Just slowly.
- **Memory usage**: Should be flat (bounded by buffer size × item size). If memory keeps growing, you have an unbounded allocation somewhere.

### Test 4: Thread Sanitizer

Compile with `-fsanitize=thread` (GCC/Clang):

```bash
g++ -fsanitize=thread -g -O1 -std=c++17 main.cpp -o main
```

Or on Windows with MSVC, use `/fsanitize=address` (MSVC doesn't have TSan, but ASan catches some issues):

```bash
cl /fsanitize=address /EHsc /std:c++17 main.cpp
```

Thread Sanitizer will catch any data races at runtime. If your synchronization is correct, it should report zero issues. If it reports a race, it will show you exactly which two threads accessed the same memory without synchronization.

### Test 5: Stress Test with Large Files

Create a large test file:

```python
# generate_test_data.py
with open("data/stress_test.txt", "w") as f:
    for i in range(100000):
        f.write(f"word_{i} another_{i} third_{i} fourth_{i}\n")
```

Run your search engine and verify:
- No crashes
- No hangs (all threads exit cleanly)
- Token count matches expected count
- Search for `word_99999` returns `stress_test.txt`

---

## Summary

| Concept | What It Means For Your Code |
|---|---|
| **Producer-Consumer** | DirectoryReader produces for Lexer, Lexer produces for Parser |
| **Bounded Buffer** | Your RingBuffer has 1024 slots — it can overflow |
| **Silent Drop** | `push()` returns false, you ignore it → data lost |
| **Back Pressure** | Producer sleeps when buffer is full, wakes when consumer pops |
| **Two CVs** | `not_full` for producers, `not_empty` for consumers |
| **Propagation** | Slow parser → full parserQueue → workers sleep → full lineQueue → Lexer::Run sleeps |
| **close()** | Replaces poison pills. Drains remaining data, then exits cleanly |

The core principle: **never ignore a failed push.** Either retry it or make it impossible to fail. `push_blocking` makes it impossible to fail — it waits until it can succeed.
