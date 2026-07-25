# C++ Concurrency Primitives — The Building Blocks

Everything in this document exists because of one fact:

**When two threads touch the same memory, and at least one of them writes, the program is broken unless you do something about it.**

This is called a **data race**. Every primitive in this document exists to prevent data races while still allowing threads to cooperate.

---

## Table of Contents

1. [Why Concurrency is Hard](#1-why-concurrency-is-hard)
2. [std::mutex — The Lock](#2-stdmutex--the-lock)
3. [std::lock_guard — Automatic Locking](#3-stdlock_guard--automatic-locking)
4. [std::unique_lock — Flexible Locking](#4-stdunique_lock--flexible-locking)
5. [std::condition_variable — Waiting for Something to be True](#5-stdcondition_variable--waiting-for-something-to-be-true)
6. [std::atomic — Lock-Free Shared State](#6-stdatomic--lock-free-shared-state)
7. [Memory Ordering — Why Your RingBuffer Says "acquire" and "release"](#7-memory-ordering)
8. [How These Connect in Your Codebase](#8-how-these-connect-in-your-codebase)

---

## 1. Why Concurrency is Hard

Consider this:

```cpp
int counter = 0;

// Thread A
counter++;

// Thread B
counter++;
```

You'd expect `counter == 2`. But `counter++` is **not one operation**. It's three:

```
1. Read counter from memory into a CPU register
2. Add 1 to the register
3. Write the register back to memory
```

If both threads execute simultaneously:

```
Thread A: read counter (0)
Thread B: read counter (0)      ← both read 0
Thread A: add 1 → register = 1
Thread B: add 1 → register = 1
Thread A: write 1 to counter
Thread B: write 1 to counter    ← overwrites A's write
```

Result: `counter == 1`. You lost an increment. This is a **data race**.

The deeper problem is that modern CPUs make this even worse:

- **CPU caches**: Each core has its own L1/L2 cache. Thread A's write might sit in Core 0's cache and never be visible to Thread B on Core 1.
- **Compiler reordering**: The compiler can rearrange your code for optimization. It might move a write before a read if it thinks they're independent.
- **CPU reordering**: The CPU itself reorders instructions in its pipeline for performance.

So you need two guarantees:
1. **Mutual exclusion** — only one thread in a critical section at a time
2. **Visibility** — when one thread writes, other threads can actually see the new value

Every primitive below provides one or both of these guarantees.

---

## 2. std::mutex — The Lock

A mutex (mutual exclusion) is the most fundamental synchronization primitive. It guarantees that only one thread can hold the lock at a time.

```cpp
#include <mutex>

std::mutex mtx;
int shared_data = 0;

void thread_work() {
    mtx.lock();         // Acquire the lock — blocks if another thread holds it
    shared_data++;      // Safe: only one thread can be here at a time
    mtx.unlock();       // Release the lock — other threads can now acquire it
}
```

### What actually happens at the hardware level

When you call `mtx.lock()`:
1. The CPU executes an **atomic compare-and-swap** instruction (like `lock cmpxchg` on x86)
2. If the mutex is free, the CAS succeeds, and the thread now owns the lock
3. If the mutex is held by another thread, the OS **puts your thread to sleep** — it's removed from the CPU's run queue
4. A **memory barrier** is issued, ensuring all writes from the previous lock holder are visible

When you call `mtx.unlock()`:
1. A **memory barrier** flushes all writes made under the lock
2. The lock is released
3. The OS **wakes up one of the sleeping threads** waiting for this lock

> **IMPORTANT:**
> A mutex doesn't just protect code — it also synchronizes memory. After acquiring a lock, you're guaranteed to see all writes made by whoever held the lock before you. This is crucial.

### The Danger: Forgetting to Unlock

```cpp
void dangerous() {
    mtx.lock();
    
    if (some_condition) {
        return;  // BUG: mutex is never unlocked!
    }
    
    // ... more work ...
    mtx.unlock();
}
```

If the function returns early (or throws an exception), the mutex stays locked **forever**. Every other thread waiting on it will be stuck. This is a **deadlock**.

This is why you almost never call `lock()` and `unlock()` directly. Instead, you use RAII wrappers.

---

## 3. std::lock_guard — Automatic Locking

`lock_guard` locks the mutex in its constructor and unlocks it in its destructor. Since C++ guarantees destructors run when an object goes out of scope (even during exceptions or early returns), the mutex is always released.

```cpp
void safe() {
    std::lock_guard<std::mutex> lock(mtx);  // locks mtx
    
    shared_data++;
    
    if (some_condition) {
        return;  // destructor of 'lock' runs here → mtx.unlock()
    }
    
    // ... more work ...
    
}   // destructor of 'lock' runs here → mtx.unlock()
```

This is what you use in your lexer at `lexer.cpp line 96`:

```cpp
std::lock_guard<std::mutex> lock(g_parserMutex);
parserQueue.push(token);
```

### When to Use lock_guard

Use `lock_guard` when:
- You need to hold the lock for the entire scope
- You don't need to unlock and re-lock
- You don't need to use a condition variable

It's the simplest, lowest-overhead option. Think of it as the "default" choice.

### The Scope Trick

You can use curly braces to limit how long the lock is held:

```cpp
void process() {
    // ... unlocked work ...
    
    {   // ← new scope starts here
        std::lock_guard<std::mutex> lock(mtx);
        shared_data++;
    }   // ← lock released here, before the expensive work below
    
    do_expensive_unrelated_work();  // runs without holding the lock
}
```

This is exactly the pattern used in your `lexer.cpp lines 141-145`:

```cpp
{
    std::lock_guard<std::mutex> lock(lineQueueMutex);
    lineQueue.push({ file, std::string(line) });
}
// Lock released before calling queueCV.notify_one()
```

---

## 4. std::unique_lock — Flexible Locking

`unique_lock` does everything `lock_guard` does, plus more:

- You can **unlock and re-lock** it
- You can **try to lock** without blocking
- You can **pass it to a condition variable** (condition variables require `unique_lock`)

```cpp
void flexible() {
    std::unique_lock<std::mutex> lock(mtx);  // locks immediately
    
    shared_data++;
    
    lock.unlock();          // explicit unlock
    do_unrelated_work();
    lock.lock();            // re-lock
    
    shared_data--;
}   // destructor unlocks if still locked
```

### Why condition_variable Requires unique_lock

A condition variable needs to **atomically unlock the mutex and go to sleep**. Then when it wakes up, it needs to **re-lock the mutex**. `lock_guard` can't do either of these things — it only unlocks in its destructor. `unique_lock` can.

This is why your `lexer.cpp line 64` uses `unique_lock`:

```cpp
std::unique_lock<std::mutex> lock(lineQueueMutex);

queueCV.wait(lock, [this] { 
    return !lineQueue.empty() || !running.load(std::memory_order_acquire); 
});
```

The `wait()` call:
1. **Unlocks** `lineQueueMutex` (so the producer can push items)
2. **Puts the thread to sleep**
3. When notified: **re-locks** `lineQueueMutex` and checks the predicate
4. If the predicate is true, returns (with the lock held)
5. If the predicate is false, goes back to step 1 (spurious wakeup)

`lock_guard` cannot do step 1 or 3. That's why `unique_lock` exists.

### The Key Difference Visualized

```
lock_guard lifecycle:
    ┌─── constructor: lock() ───┐
    │                           │
    │   (your code runs here)   │
    │                           │
    └─── destructor: unlock() ──┘
    
    That's it. No flexibility.

unique_lock lifecycle:
    ┌─── constructor: lock() ───┐
    │                           │
    │   lock.unlock()  ←────── you can do this
    │   ...                     
    │   lock.lock()    ←────── and re-lock
    │                           │
    │   cv.wait(lock)  ←────── CV can unlock/re-lock internally
    │                           │
    └─── destructor: unlock() ──┘ (if still locked)
```

### When to Use Which

| Situation | Use |
|---|---|
| Simple lock-the-scope | `lock_guard` |
| Need to use `condition_variable` | `unique_lock` |
| Need to unlock/re-lock manually | `unique_lock` |
| Need `try_lock()` | `unique_lock` |

---

## 5. std::condition_variable — Waiting for Something to be True

This is the most important primitive for your back pressure problem. A condition variable lets a thread say:

> "I'm going to sleep. Wake me up when this condition becomes true."

### The Fundamental Pattern

```cpp
std::mutex mtx;
std::condition_variable cv;
bool data_ready = false;
int data = 0;

// CONSUMER — waits for data
void consumer() {
    std::unique_lock<std::mutex> lock(mtx);
    
    cv.wait(lock, [&] { return data_ready; });
    // ↑ This does:
    //   1. Check predicate: is data_ready true?
    //   2. If yes: return immediately (lock stays held)
    //   3. If no: unlock mtx, sleep, wait to be notified
    //   4. On notification: re-lock mtx, go to step 1
    
    // When we get here: lock is held AND data_ready == true
    std::cout << "Got data: " << data << std::endl;
}

// PRODUCER — produces data and wakes consumer
void producer() {
    {
        std::lock_guard<std::mutex> lock(mtx);
        data = 42;
        data_ready = true;
    }   // unlock before notifying (optimization — avoids immediate re-block)
    
    cv.notify_one();  // wake up one waiting thread
}
```

### Why the Predicate Matters (Spurious Wakeups)

You might think you can write:

```cpp
cv.wait(lock);              // wait without a predicate
// assume the condition is true here
```

**This is wrong.** The OS can wake your thread up for no reason. This is called a **spurious wakeup**. It happens because:

- The OS kernel may wake threads for internal scheduling reasons
- On some architectures, the futex system call can return early
- Multiple threads might be notified but only one should proceed

The predicate form `cv.wait(lock, predicate)` handles this automatically. It's equivalent to:

```cpp
while (!predicate()) {    // keep checking
    cv.wait(lock);        // sleep until notified
}
```

If a spurious wakeup occurs, the predicate is false, and the thread goes back to sleep. No harm done.

### What Exactly is a Spurious Wakeup?

Let's go deeper. When you call `cv.wait(lock)`, the implementation (on Linux) does roughly:

```
1. Unlock the mutex
2. Call futex(FUTEX_WAIT) — a kernel system call that puts the thread to sleep
3. (thread is sleeping)
4. Kernel wakes the thread (via futex(FUTEX_WAKE) from another thread's notify)
5. Re-lock the mutex
6. Return from wait()
```

A spurious wakeup happens when the kernel returns from step 4 **without another thread actually calling notify**. Why would this happen?

- **Signal interruption**: If a Unix signal is delivered to the thread, the futex syscall can return with `EINTR`. The C++ runtime may translate this into returning from `wait()`.
- **Implementation choice**: The POSIX standard explicitly allows `pthread_cond_wait` to return spuriously. This gives implementers freedom to use more efficient algorithms that might occasionally produce false wakeups.
- **Multiprocessor race**: On some architectures, the memory operation that checks "should I still sleep?" can race with a concurrent modification, causing the thread to wake.

The bottom line: **always use the predicate form.** Never assume a wakeup means the condition is true.

```cpp
// WRONG — vulnerable to spurious wakeups
cv.wait(lock);

// CORRECT — predicate re-checks after every wakeup
cv.wait(lock, [&] { return !queue.empty(); });
```

### notify_one vs notify_all

- `notify_one()` — wakes **one** waiting thread. Use when only one thread should proceed (e.g., one consumer grabs one item).
- `notify_all()` — wakes **all** waiting threads. Use when the condition might affect multiple threads (e.g., shutdown signal).

> **WARNING:**
> If you use `notify_one()` but multiple threads are waiting and the condition is true for all of them, only one wakes up. The others stay asleep until the next notification. This can cause stalls if the producer doesn't keep notifying.

### The Unlock-Before-Notify Optimization

You'll see this pattern frequently:

```cpp
{
    std::lock_guard<std::mutex> lock(mtx);
    queue.push(item);
}                          // ← unlock HERE
cv.notify_one();           // ← notify AFTER unlock
```

Why not notify inside the lock? Because if you do:

```cpp
{
    std::lock_guard<std::mutex> lock(mtx);
    queue.push(item);
    cv.notify_one();       // wakes a consumer...
}                          // ...but the consumer immediately blocks on mtx
```

The consumer wakes up, tries to lock `mtx`, and immediately goes back to sleep because the producer still holds it. You've wasted a context switch. Unlocking first avoids this.

Your code already does this correctly in `lexer.cpp L141-147`:

```cpp
{
    std::lock_guard<std::mutex> lock(lineQueueMutex);
    lineQueue.push({ file, std::string(line) }); 
}
queueCV.notify_one();   // notify after releasing lock ✓
```

### The Complete Mental Model

Think of a condition variable as a **waiting room**:

```
                    ┌─────────────────────────┐
                    │      Waiting Room        │
                    │   (condition_variable)   │
                    │                          │
    thread.wait() →│  💤 Thread A (sleeping)   │
                    │  💤 Thread B (sleeping)   │
                    │  💤 Thread C (sleeping)   │
                    │                          │
                    └──────────┬──────────────┘
                               │
                    notify_one() → wakes ONE thread
                    notify_all() → wakes ALL threads
```

The mutex is the **door to the room**. You must hold the mutex to enter the waiting room (call `wait()`). When you go to sleep, you drop the key (unlock). When you're woken up, you pick the key back up (re-lock) before leaving.

---

## 6. std::atomic — Lock-Free Shared State

An atomic variable guarantees that reads and writes are **indivisible** — no other thread can see a half-written value.

```cpp
#include <atomic>

std::atomic<int> counter{0};

// Thread A
counter++;   // atomic increment — no lock needed

// Thread B
counter++;   // safe, guaranteed no data race

// Always reads 2 (given both threads complete)
```

### How it Works at the Hardware Level

On x86, `counter++` on an `std::atomic<int>` compiles to:

```asm
lock add DWORD PTR [counter], 1
```

The `lock` prefix tells the CPU: "No other core can access this memory address until this instruction completes." The CPU does this by locking the cache line.

For larger types (e.g., `std::atomic<SomeBigStruct>`), the compiler may fall back to a **spinlock** internally because the hardware can only atomically operate on small values (typically up to 8 bytes on 64-bit systems). You can check this with:

```cpp
std::atomic<int> a;
std::cout << a.is_lock_free() << std::endl;  // 1 = truly lock-free, 0 = uses internal lock
```

### atomic vs mutex — When to Use Which

| `std::atomic` | `std::mutex` |
|---|---|
| Protects **a single variable** | Protects **a region of code** (multiple variables, complex logic) |
| Lock-free (no sleeping) | Can put threads to sleep |
| Only supports simple operations (load, store, increment, CAS) | Supports arbitrary code |
| Very fast (~1-10ns) | Slower (~25-100ns) |

You use atomics in your ring buffer for the read/write indices. You use a mutex in the lexer worker for protecting the queue + condition variable pattern.

### Load and Store

For simple reads/writes, use `load()` and `store()`:

```cpp
std::atomic<bool> running{true};

// Thread A (producer)
running.store(false, std::memory_order_release);

// Thread B (consumer)
if (running.load(std::memory_order_acquire)) {
    // still running
}
```

This is what your `lexer.hpp line 46` does:

```cpp
std::atomic<bool> running{true};
```

And your `lexer.cpp line 60` checks it:

```cpp
while(running.load(std::memory_order_acquire)){
```

The `memory_order_acquire` and `memory_order_release` are **memory orderings** — they control what other memory operations are visible. This is the deepest and most important concept.

### Compare-and-Swap (CAS) — The Foundation of Lock-Free Programming

CAS is the most powerful atomic operation. It does this atomically:

```cpp
// Pseudocode for what CAS does (but atomically, in one CPU instruction)
bool compare_and_swap(atomic<int>& target, int& expected, int desired) {
    if (target == expected) {
        target = desired;
        return true;       // success — we swapped
    } else {
        expected = target;  // update expected with actual value
        return false;      // failure — someone else changed it
    }
}
```

In C++:

```cpp
std::atomic<int> value{0};

int expected = 0;
bool success = value.compare_exchange_strong(expected, 1);
// If value was 0: sets it to 1, returns true
// If value was NOT 0: sets expected to current value, returns false
```

This is how mutexes are built internally. It's also how you'd build a lock-free stack, queue, or other data structure. You don't need CAS for your current project, but understanding that it exists helps you see how the whole stack fits together:

```
Your code
    ↓
std::mutex (uses CAS internally)
    ↓
std::atomic (uses lock prefix / CAS CPU instructions)
    ↓
CPU hardware (cache coherency protocol — MESI/MOESI)
```

---

## 7. Memory Ordering

This is the hardest concept in C++ concurrency and the most important one for understanding your ring buffer. Take your time with this section.

### The Problem: CPUs and Compilers Reorder Operations

Consider:

```cpp
int data = 0;
bool ready = false;

// Thread A (producer)
data = 42;          // (1)
ready = true;       // (2)

// Thread B (consumer)
if (ready) {        // (3)
    use(data);      // (4) — expects data == 42
}
```

You'd expect that if Thread B sees `ready == true`, it also sees `data == 42`. But:

- The **compiler** might reorder (1) and (2) because they're independent variables
- The **CPU** might reorder writes — (2) could become visible to other cores before (1)
- Thread B's **CPU cache** might have a stale value of `data`

Without memory ordering guarantees, Thread B could see `ready == true` but `data == 0`. This is catastrophic.

### Why Do CPUs Reorder?

CPUs reorder for performance. A modern CPU has a **store buffer** — writes don't go directly to cache/memory. They sit in a buffer and get flushed later. This means:

```
Thread A writes: data = 42, then ready = true
Store buffer:    [ready = true] [data = 42]   ← buffer might flush ready first!
Other cores see: ready = true ... (data still stale)
```

The store buffer can flush in any order unless you issue a **memory barrier** (fence) that forces ordering.

On x86, the reordering is actually limited — stores are not reordered with other stores (but loads CAN be reordered with older stores). On ARM and RISC-V, both loads and stores can be reordered freely. C++ memory orderings give you a portable way to control this.

### Memory Orderings Explained

C++ gives you five memory orderings. You need to understand three:

#### `memory_order_relaxed` — No ordering guarantees

```cpp
// Only guarantees: the operation itself is atomic (no torn reads/writes)
// Does NOT guarantee: anything about the order relative to other operations

counter.store(1, std::memory_order_relaxed);
```

Use when you only need atomicity, not ordering. Example: a simple counter that no other logic depends on.

#### `memory_order_release` — "Publish" semantics

```cpp
// Guarantee: all memory writes BEFORE this store 
// are visible to any thread that does an acquire-load of this variable

data = 42;                                          // write A
flag.store(true, std::memory_order_release);        // RELEASE barrier
// ↑ Guarantee: write A is visible before this store is visible
```

Think of it as: "I'm **publishing** my work. Everything I wrote before this point is now committed and visible."

The compiler and CPU are forbidden from moving any writes past (after) a release store.

```
Memory writes:
    ┌──────────────────────┐
    │  data = 42           │  ← these cannot move below the barrier
    │  other_data = 99     │
    ├──────────────────────┤  ← RELEASE BARRIER
    │  flag.store(RELEASE) │
    └──────────────────────┘
```

#### `memory_order_acquire` — "Subscribe" semantics

```cpp
// Guarantee: all memory reads AFTER this load 
// see the writes that happened before the corresponding release-store

if (flag.load(std::memory_order_acquire)) {         // ACQUIRE barrier
    use(data);                                       // read A
    // ↑ Guarantee: sees the data written before the release
}
```

Think of it as: "I'm **subscribing** to the publisher's work. Everything they wrote before their release is now visible to me."

The compiler and CPU are forbidden from moving any reads before (above) an acquire load.

```
Memory reads:
    ┌──────────────────────┐
    │  flag.load(ACQUIRE)  │
    ├──────────────────────┤  ← ACQUIRE BARRIER
    │  use(data)           │  ← these cannot move above the barrier
    │  use(other_data)     │
    └──────────────────────┘
```

#### The Acquire-Release Pair

Release and acquire work as a **pair**. They create a "happens-before" relationship:

```
Thread A                              Thread B
─────────                             ─────────
data = 42;                            
slots[0] = item;                      
write_index.store(1, RELEASE); ──────→ write_index.load(ACQUIRE);
                                      // now Thread B sees data == 42
                                      // and sees slots[0] == item
```

Everything before the release-store **happens-before** everything after the acquire-load (of the same atomic variable).

### Applied to Your Ring Buffer

Now look at your `ringbuffer.hpp` with this understanding:

```cpp
bool push(const T& item){
    size_t write = write_index.load(std::memory_order_relaxed);   // (1) just read our own index
    size_t read = read_index.load(std::memory_order_acquire);      // (2) ACQUIRE: see consumer's latest pop

    if ((write - read) >= SIZE){
        return false;                                              // buffer full
    };

    slots[write & (SIZE - 1)] = item;                              // (3) write the data

    write_index.store(write + 1, std::memory_order_release);       // (4) RELEASE: publish the data
    return true;
};
```

Here's what each ordering does:

| Line | Ordering | Why |
|---|---|---|
| (1) `write_index.load(relaxed)` | `relaxed` | We're the **only writer** of `write_index`, so we always see our own latest value. No ordering needed. |
| (2) `read_index.load(acquire)` | `acquire` | We need to see the consumer's latest `pop`. The consumer does a release-store on `read_index` after reading the slot. Our acquire-load ensures we see that the slot is now free. |
| (3) `slots[write] = item` | (normal write) | This is the actual data. It **must** be visible before we update `write_index`. |
| (4) `write_index.store(release)` | `release` | PUBLISH: guarantees that the slot write (3) is visible to any thread that acquire-loads `write_index`. The consumer will see the data. |

And `pop`:

```cpp
bool pop(T& item){
    size_t read = read_index.load(std::memory_order_relaxed);      // our own index
    size_t write = write_index.load(std::memory_order_acquire);    // ACQUIRE: see producer's latest push

    if (read >= write){
        return false;
    };

    item = slots[read & (SIZE - 1)];                               // read the data

    read_index.store(read + 1, std::memory_order_release);         // RELEASE: publish that we've consumed the slot
    return true;
};
```

The symmetry:

```
Producer                                Consumer
────────                                ────────
slots[w] = item;         ───────→       
write_index.store(RELEASE) ──PUBLISH──→ write_index.load(ACQUIRE)
                                        item = slots[r];
                                        read_index.store(RELEASE) ──PUBLISH──→ read_index.load(ACQUIRE)
                                                                               // producer sees slot is free
```

Each side **publishes** its work with a release-store, and the other side **subscribes** with an acquire-load. The data flows correctly without any mutex.

### The Full Picture — How a Single Item Flows Through the Ring Buffer

Let's trace one push/pop cycle with memory ordering:

```
PUSH (Producer on Core 0):
─────────────────────────
1. write_index.load(relaxed) → 5         // "what slot should I write to?"
2. read_index.load(acquire)  → 3         // "has the consumer freed up space?"
3. (5 - 3) = 2 < 1024, so there's room
4. slots[5 & 1023] = item                // write data to slot 5
5. write_index.store(6, release)          // "I'm done writing slot 5"
                                          // RELEASE ensures step 4 is visible
                                          // before step 5 is visible

POP (Consumer on Core 1):
─────────────────────────
1. read_index.load(relaxed)   → 3        // "what slot should I read from?"
2. write_index.load(acquire)  → 6        // "has the producer written anything?"
                                          // ACQUIRE ensures we see the data
                                          // the producer wrote before their release
3. 3 < 6, so there's data
4. item = slots[3 & 1023]                // read data from slot 3
5. read_index.store(4, release)           // "I'm done reading slot 3"
                                          // RELEASE ensures step 4 completes
                                          // before producer sees slot 3 is free
```

### Why memory_order_seq_cst Exists (and When You'd Use It)

`memory_order_seq_cst` (sequentially consistent) is the strongest ordering. It's the **default** if you don't specify an ordering:

```cpp
counter.fetch_add(1);  // implicitly seq_cst
// equivalent to:
counter.fetch_add(1, std::memory_order_seq_cst);
```

It guarantees a single **total order** — all threads agree on the order of all seq_cst operations. This is the easiest to reason about but the most expensive (on non-x86 architectures, it emits full memory fences).

**Rule of thumb**: Start with `seq_cst` (the default). Only use weaker orderings when you understand exactly why and you've profiled a bottleneck. Your ring buffer uses `acquire`/`release` because it's a known SPSC pattern that's been well-analyzed.

### Summary Table

| Ordering | Guarantees | Cost | Use When |
|---|---|---|---|
| `relaxed` | Atomicity only | Cheapest | Counters, flags with no dependencies |
| `acquire` | Sees writes before matching release | Low | Consumer reading shared data |
| `release` | Makes writes visible to matching acquire | Low | Producer publishing shared data |
| `acq_rel` | Both acquire and release | Medium | Read-modify-write on shared data |
| `seq_cst` | Total order across all threads | Highest | When you need the strongest guarantee or aren't sure |

---

## 8. How These Connect in Your Codebase

Here's how every primitive maps to your search engine pipeline:

| Primitive | Where You Use It | Purpose |
|---|---|---|
| `std::atomic<size_t>` | `ringbuffer.hpp L9-10` | Lock-free read/write indices for the ring buffer |
| `memory_order_acquire/release` | `ringbuffer.hpp` throughout | Ensures slot data is visible across threads without a mutex |
| `std::mutex` | `lexer.hpp L44` | Protects `lineQueue` access for condition variable use |
| `std::lock_guard` | `lexer.cpp L96`, `L143` | RAII mutex locking for queue pushes |
| `std::unique_lock` | `lexer.cpp L64` | Required by `condition_variable::wait()` |
| `std::condition_variable` | `lexer.hpp L45` | Workers sleep until lineQueue has data |
| `std::atomic<bool>` | `lexer.hpp L46` | Shutdown flag — workers check this to know when to exit |

### The Two Synchronization Strategies in Your Code

Your codebase currently uses **two different synchronization strategies** side by side:

**Strategy 1 — Lock-free (Ring Buffer)**:
```
push(): atomic write to slot → atomic release-store to write_index
pop():  atomic acquire-load of write_index → read slot → atomic release-store to read_index
```
No mutex, no sleeping. If the buffer is full, `push` returns `false` immediately. If empty, `pop` returns `false` immediately. **Fast, but no waiting.**

**Strategy 2 — Mutex + Condition Variable (Lexer Workers)**:
```
producer: lock mutex → push to queue → unlock → notify CV
consumer: lock mutex → wait on CV until queue non-empty → pop from queue → unlock
```
Uses a mutex and CV. If the queue is empty, the consumer sleeps until notified. **Slower, but can wait.**

The key insight: **your ring buffer (lock-free atomics) and your worker threads (mutex + condition variable) use different synchronization strategies.** The ring buffer is fast but can't block. The workers can block but use heavier synchronization.

To add back pressure, you need to combine them — use a condition variable to make the ring buffer's `push` block when full. That's what the next document is about.
