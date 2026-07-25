# Concurrency Approach 1: Synchronization Primitives — Locks, Signals, and Atomics

Welcome to the first deep-dive in the concurrency series, Ntshembo. You’ve built a search engine, and you’re hitting the wall that every systems engineer eventually hits: multi-threading is hard. 

In this lesson, we are going to tear down the abstractions. We'll look at the fundamental building blocks of concurrent programming in C++—Mutexes, Condition Variables, and Atomics—using the actual code you wrote in your search engine project. We'll diagnose the spin-lock bugs, the deadlocks, and the starvation issues that are currently causing your engine to hang.

> "Concurrency is not parallelism. Concurrency is about dealing with lots of things at once. Parallelism is about doing lots of things at once." — Rob Pike

Let's dive in.

---

## Section 1: "Why Node.js Never Gave You This Problem"

Coming from Node.js, you have an incredible intuition for asynchronous flows. You know exactly how to juggle network requests, file I/O, and database queries without blocking the main thread. 

But here is the massive paradigm shift you need to make: **In Node.js, there is only one thread touching your data.**

The V8 engine runs a single main thread. The Event Loop is the great synchronizer. When you write an `await`, V8 takes your current function, packs it up, and puts it aside. It then pulls the next callback off the microtask queue and runs it. 
*Because callbacks never interrupt each other, they never fight over the same variables.*

Imagine if every `await` in your Node code could run its callback at the EXACT same time as another `await`'s callback — on a completely different CPU core. 
Imagine if, while one callback is iterating over an array, another callback is `push()`-ing into it at that exact nanosecond. 
That's what threads do in C++.

In Node.js, the runtime synchronizes the code for you. In C++, **you ARE the synchronizer**. You have to manually tell the operating system when it's safe for two threads to touch the same memory, and when one thread needs to back off.

---

## Section 2: The Mutex — Your First Lock

The most fundamental synchronization primitive is the **Mutex** (Mutual Exclusion). You've used this in your global definition file: `internal/kernal/core/headerfiles/sharedmutexs.hpp` and `internal/lexer/lexer.cpp`.

### What is a Mutex mechanically?
At its core, a `std::mutex` is a flag in memory backed by OS kernel support.
- **`lock()`**: The thread checks the flag. If it's free, it sets the flag and proceeds. If it's NOT free, the OS intervenes. The OS physically removes your thread from the CPU core, puts it to sleep, and schedules something else. This is expensive, but it saves CPU cycles compared to infinitely checking the flag.
- **`unlock()`**: The thread clears the flag. The OS looks at the queue of sleeping threads waiting for this lock, picks one, and wakes it up.

The code executed between `lock()` and `unlock()` is called the **critical section**.

Here is an ASCII timeline diagram showing what happens when two threads try to access the same queue without and with a mutex:

```text
WITHOUT MUTEX (Data Corruption!)
Time | Thread 1 (Worker 1)          | Thread 2 (Worker 2)
-----+------------------------------+------------------------------
 1   | read size: 10                | read size: 10
 2   | write slots[10] = tokenA     | 
 3   |                              | write slots[10] = tokenB  <-- OVERWRITES tokenA!
 4   | write size: 11               | write size: 11            <-- SIZE IS WRONG!

WITH MUTEX (Correct but Serialized)
Time | Thread 1 (Worker 1)          | Thread 2 (Worker 2)
-----+------------------------------+------------------------------
 1   | lock() -> SUCCEEDS           | lock() -> BLOCKS (OS puts to sleep)
 2   | read size: 10                | (sleeping...)
 3   | write slots[10] = tokenA     | (sleeping...)
 4   | write size: 11               | (sleeping...)
 5   | unlock() -> wakes Thread 2   | (waking up...)
 6   | (doing other work)           | lock() -> SUCCEEDS
 7   |                              | read size: 11
 8   |                              | write slots[11] = tokenB
 9   |                              | write size: 12
 10  |                              | unlock()
```

### RAII Wrappers: `std::lock_guard` vs `std::unique_lock`
In C++, if a function throws an exception or returns early while holding a lock, the `unlock()` might never happen. This is a fatal deadlock. 
To fix this, we use RAII (Resource Acquisition Is Initialization).

- `std::lock_guard<std::mutex> lock(myMutex);` — Simple, lightweight. Locks when created, unlocks when destroyed (at the end of the `{}` scope). You cannot manually unlock it early.
- `std::unique_lock<std::mutex> lock(myMutex);` — Flexible, slightly heavier. Can be manually locked/unlocked, and is required for condition variables (which we'll cover soon).

In `internal/lexer/lexer.cpp`, you correctly use this:
```cpp
// Inside Lexer::Run()
std::lock_guard<std::mutex> lock(lineQueueMutex);
lineQueue.push({ file, std::string(line) }); 
```

> **CRITICAL INSIGHT:** The lock protects **DATA**, not **CODE**. 
> A mutex doesn't magically make a function safe — it makes the DATA that function touches safe. But this ONLY works if EVERY SINGLE ACCESS to that data (from any thread) goes through the exact same mutex. If one thread locks to write, but another thread reads without locking, you still have a data race.

---

## Section 3: Lock Scope — The Biggest Beginner Mistake

Let's look at the bug currently crippling your parser. It's a classic mistake: expanding the lock scope too far.

In `internal/parser/parser.cpp`, you have this code:

```cpp
void Parser::Run(){
    while(true){
        std::string token;
        std::lock_guard<std::mutex> lock(g_parserMutex);  // BUG: scope too wide
        if (parserQueue.pop(token)){
            if (token.empty()) { break; }
            std::cout << "Token: " << token << std::endl;
        }
    }
}
```

Remember that the scope of a `lock_guard` is from its declaration to the end of its enclosing `{}` block. 
Here, the enclosing block is the entire body of the `while(true)` loop!

What happens mechanically?
1. The Parser locks `g_parserMutex`.
2. It checks `parserQueue`. It's empty.
3. The loop iteration ends, the `lock_guard` goes out of scope, and the mutex unlocks.
4. **NANOSECONDS LATER**, the `while(true)` loop restarts, and the Parser instantly re-locks the mutex.

Let's look at the ASCII timeline of what this does to your Lexer workers:

```text
Time | Parser Thread                      | Lexer Worker Thread
-----+------------------------------------+-----------------------------------
 1   | lock(g_parserMutex) -> SUCCESS     | 
 2   | pop() -> false                     | tries to lock(g_parserMutex)
 3   | unlock(g_parserMutex)              | -> BLOCKED by Parser
 4   | loop restarts                      | (OS trying to wake Worker)
 5   | lock(g_parserMutex) -> SUCCESS     | (Worker still waking up)
 6   | pop() -> false                     | (Worker awake, tries to lock)
 7   | unlock(g_parserMutex)              | -> BLOCKED AGAIN by Parser
 8   | loop restarts                      | (Worker goes back to sleep)
```

This is called **STARVATION**. The parser is technically releasing the lock, but it re-acquires it so blindingly fast that the lexer workers never get a turn to push tokens into the queue! It's burning 100% of a CPU core doing absolutely nothing, and choking the rest of the system.

**The Node.js Analogy:**
It's like having a `setInterval(checkQueue, 0)` that starves every other callback because it keeps jumping to the front of the microtask queue, never letting the system do actual I/O.

### The Fix: Minimize Lock Scope
You only need to protect the actual shared data structure (`parserQueue`).

```cpp
void Parser::Run(){
    while(true){
        std::string token;
        bool hasToken = false;
        
        { // <--- BEGIN minimal scope
            std::lock_guard<std::mutex> lock(g_parserMutex);
            hasToken = parserQueue.pop(token);
        } // <--- END minimal scope. Mutex is unlocked NOW.
        
        if (hasToken){
            if (token.empty()) { break; }
            std::cout << "Token: " << token << std::endl;
        } else {
            // Queue is empty. We need to WAIT, not spin.
            std::this_thread::yield(); // Or better yet, a condition variable!
        }
    }
}
```

---

## Section 4: The Condition Variable — "Wake Me When It's Ready"

The parser's `while(true)` loop (even when fixed) is polling. Polling is bad. It's the equivalent of checking the oven every 5 seconds to see if the cake is done. 

In Node.js, you'd solve this with an `EventEmitter` or a `Promise` resolving. In C++, the equivalent synchronization primitive is the **Condition Variable** (`std::condition_variable`).

You successfully used one in `internal/lexer/lexer.cpp`! Let's deconstruct exactly how it works.

```cpp
// In Lexer::worker()
std::unique_lock<std::mutex> lock(lineQueueMutex);
queueCV.wait(lock, [this] { return !lineQueue.empty(); });
```

### How `cv.wait()` works mechanically:
This is one of the hardest concepts to grasp because it does three things atomically:

1. **Evaluates the Predicate**: It runs the lambda `[this] { return !lineQueue.empty(); }`. If it returns `true` (the queue has data), `wait()` returns immediately. The thread keeps the lock and proceeds.
2. **Sleeps and Unlocks**: If the predicate is `false` (the queue is empty), `wait()` does something magical: it **RELEASES the lock AND puts the thread to sleep in a single atomic step**. There is no gap where the lock is released but the thread is still running.
3. **Wakes and Re-locks**: When another thread calls `queueCV.notify_one()` or `notify_all()`, the OS wakes this thread up. The very first thing the thread does before fully waking up is **re-acquire the lock**. Once locked, it re-evaluates the predicate. If true, it returns. If false, it goes back to sleep.

> **Why `unique_lock`?** This is why `std::condition_variable` requires a `std::unique_lock` instead of a `std::lock_guard`. The condition variable needs the ability to unlock and re-lock the mutex internally while you wait.

### The Predicate is NOT Optional: Spurious Wakeups
Why do we pass a lambda? Why not just `queueCV.wait(lock)`?
Because operating systems suffer from **Spurious Wakeups**. Sometimes the OS will wake your thread up for literally no reason. If you don't use a `while` loop or a predicate to check if the condition is *actually* met, your thread will proceed with an empty queue and crash. The lambda predicate is basically syntactic sugar for:
```cpp
while (!(!lineQueue.empty())) {
    queueCV.wait(lock);
}
```

### Diagnosing Your Worker Bug
Let's look at your Lexer worker code again:
```cpp
queueCV.wait(lock, [this] { return !lineQueue.empty(); });
if (!running.load(std::memory_order_acquire) && lineQueue.empty()) { break; }
```
Do you see the logical trap here? 
When you shut down the system, `running` becomes `false`. The queue drains and becomes empty. 
The worker loop restarts. It hits `queueCV.wait()`. 
The predicate `!lineQueue.empty()` evaluates to `false`. 
The thread goes to sleep... **FOREVER**. 
It never reaches the `break` statement on the next line because it is permanently asleep waiting for an item that will never come.

**The Fix:** Your wait predicate must encompass ALL conditions that should wake the thread, including shutdown!
```cpp
queueCV.wait(lock, [this] { 
    // Wake up IF the queue has items, OR if we are shutting down.
    return !lineQueue.empty() || !running.load(std::memory_order_acquire); 
});
```

### The Notify Optimization
In `Lexer::Run()`, you have this:
```cpp
std::lock_guard<std::mutex> lock(lineQueueMutex);
lineQueue.push({ file, std::string(line) }); 
queueCV.notify_one();
```
Calling `notify_one()` while STILL holding the mutex creates a "hurry up and wait" anti-pattern. 
1. `Lexer::Run()` pushes data and signals the CV.
2. The sleeping Worker wakes up!
3. The Worker immediately tries to re-acquire the `lineQueueMutex`.
4. But `Lexer::Run()` hasn't reached the end of its `{}` scope yet! It still holds the lock!
5. The Worker goes immediately back to sleep.

**The Fix:** Unlock *before* you notify.
```cpp
{
    std::lock_guard<std::mutex> lock(lineQueueMutex);
    lineQueue.push({ file, std::string(line) }); 
} // Lock released!
queueCV.notify_one(); // Wake worker. Worker immediately acquires free lock.
```

---

## Section 5: Atomics — When a Lock Is Too Heavy

Sometimes a mutex is overkill. If you just need to increment a counter or set a boolean flag, trapping into the OS kernel is thousands of times too slow.

This is where `std::atomic` comes in, which you use extensively in `internal/kernal/core/datastructures/ringbuffer.hpp`.

### What is an Atomic?
Normally, `count++` or reading a boolean is NOT atomic. On some CPU architectures, reading a 64-bit integer actually takes two 32-bit reads. If thread A writes halfway through Thread B's read, Thread B gets corrupted garbage data.

`std::atomic<T>` provides hardware-level guarantees. The CPU hardware itself ensures that the read or write is completely indivisible. No lock required.

### Memory Ordering (The Deep End)
But atomics do more than prevent torn reads. They act as **Memory Barriers**. 

In Node.js, you never needed this because the V8 engine's single thread meant all reads and writes were naturally ordered chronologically. 
With multiple CPU cores, **each core has its own L1/L2 CACHE**. If Core 1 changes a variable, Core 2 might not see it for a long time because Core 2 is reading from its own cache! Furthermore, modern CPUs will literally re-order your assembly instructions to optimize execution.

`std::atomic` forces the CPU caches to synchronize and prevents the compiler/CPU from reordering instructions past the barrier.

Let's look at your RingBuffer:
```cpp
size_t write = write_index.load(std::memory_order_relaxed);
size_t read = read_index.load(std::memory_order_acquire);
if ((write - read) >= SIZE){ return false; };
slots[write & (SIZE - 1)] = item;
write_index.store(write + 1, std::memory_order_release);
```

You are using sophisticated memory ordering here. Let's break it down:
- **`memory_order_relaxed`**: "I just need the raw value right now. I don't care about synchronizing other variables." In `push()`, only the writer thread modifies `write_index`, so loading its own index can be relaxed.
- **`memory_order_release` (The Publisher)**: `write_index.store(..., release)`. This tells the CPU: "Do not let any writes that happened *before* this line get reordered to happen *after* this line." It ensures the `item` is physically written into `slots` BEFORE the `write_index` is updated. 
- **`memory_order_acquire` (The Subscriber)**: `read_index.load(..., acquire)`. This tells the CPU: "If I read a value that someone published with `release`, guarantee that I can see all the memory they wrote before they published." 

By pairing `release` on the store and `acquire` on the load, you create a synchronized bridge between two CPU cores without ever trapping into the OS for a lock!

### Cache Lines and False Sharing
You also wrote this brilliance: `alignas(64) std::atomic<size_t> write_index{0};`

Why 64 bytes? Because CPU caches load memory in 64-byte chunks called **Cache Lines**.
If `write_index` and `read_index` sat right next to each other in memory, they would end up in the same cache line. When Thread 1 modifies `write_index`, the CPU has to invalidate the ENTIRE cache line for Core 2. Core 2 then has to fetch it from main memory just to read `read_index`. They would constantly invalidate each other's caches—a severe performance penalty known as **False Sharing**. `alignas(64)` forces them onto separate cache lines.

---

## Section 6: The RingBuffer — Your Lock-Free Queue

Your RingBuffer is an elegant piece of engineering. Let's look at why it exists and its strict limitations.

1. **Why ring buffers?** It’s a bounded buffer. It allocates memory exactly once upfront (`T slots[SIZE]`). Because memory is contiguous, it is incredibly cache-friendly compared to a node-based linked list (like `std::queue`).
2. **The Power-of-2 Trick**: You enforce `static_assert((SIZE & (SIZE - 1)) == 0)`. This guarantees SIZE is a power of 2. Why? So you can write `slots[write & (SIZE - 1)]`. 
   Using a bitwise AND (`&`) takes about 1 CPU cycle. Using modulo (`write % SIZE`) requires division, taking 20-40 CPU cycles. In a hot loop, that's a massive optimization.
3. **The SPSC Constraint (Single-Producer, Single-Consumer)**: Your lock-free ring buffer is ONLY safe if exactly ONE thread calls `push()` and exactly ONE thread calls `pop()`. 
   What happens if two Lexer workers call `pop()` at the exact same time?
   - Worker 1 loads `read_index` (e.g., 5).
   - Worker 2 loads `read_index` (e.g., 5).
   - Both read `slots[5]`.
   - Both update `read_index` to 6.
   - Result: Both workers processed the same data, and slot 6 is skipped completely. Data corruption!

### The Mutex Trade-off
Because you have TWO Lexer workers popping from `lineQueue`, your lock-free queue is no longer safe. You correctly recognized this and wrapped `lineQueueMutex` around it in `lexer.cpp`. 

While this prevents corruption, it entirely defeats the purpose of a lock-free data structure! You are paying the overhead of atomics AND the overhead of a mutex. 

On the other hand, your `dirQueue` is used beautifully. `DirectoryReader` pushes, `Lexer::Run` pops. One producer, one consumer. No mutex needed. Pure lock-free speed.

---

## Section 7: Putting It All Together — The Bug Walkthrough

So, why does your engine hang? Let's take everything we've learned and build a timeline of your program's execution leading to the fatal deadlock.

```text
t=0ms    main() starts all threads (directory, lexer, parser).
t=1ms    DirectoryReader::Run() starts, pushes files to dirQueue lock-free.
t=2ms    Lexer::Run() starts, pops files from dirQueue, reads lines, pushes ILPs to lineQueue.
t=2ms    Lexer::Start() had already spawned worker1 and worker2.
t=3ms    worker1 wakes up, locks lineQueueMutex, pops a line, unlocks. Processes tokens.
t=3ms    worker1 tries to lock g_parserMutex to push to parserQueue.
t=3ms    Meanwhile, Parser::Run() has started. It is in its tight `while(true)` spin,
         constantly locking and unlocking g_parserMutex. (STARVATION BEGINS)
t=3ms    worker1 BLOCKS waiting for g_parserMutex. It cannot acquire it.
t=3ms    worker2 wakes up, processes a line, and also BLOCKS on g_parserMutex.
t=4ms    Lexer::Run() keeps reading files and filling lineQueue.
t=5ms    Because the workers are blocked, lineQueue fills up (capacity reached).
         Lexer::Run() push() returns false, or it finishes all files.
t=6ms    Lexer::Run() exits its while loop. 
         FATAL FLAW 1: `running.store(false)` is COMMENTED OUT in your code!
         FATAL FLAW 2: `queueCV.notify_all()` is never called to wake sleeping workers!
t=7ms    Eventually, the OS scheduler forcefully interrupts the Parser and gives 
         worker1 a chance to acquire g_parserMutex. worker1 pushes a token.
t=8ms    Over time, worker1 and worker2 slowly drain lineQueue while fighting the Parser for the lock.
t=9ms    lineQueue becomes empty.
t=10ms   worker1 and worker2 call `queueCV.wait()`.
         Because `running` is still true (commented out!), the predicate fails.
         The workers go to sleep FOREVER.
t=∞      main() calls lexer_thread.join() — waits forever for workers that will never wake.
         main() would call dir_reader_thread.join() — DirectoryReader finished.
         main() would call parser_thread.join() — parser spins forever in `while(true)`.
         PROGRAM HANGS.
```

You have a perfect storm: Lock Starvation (Parser) causing Queue Backpressure (Lexer), combined with improper shutdown signaling (Condition Variable predicate failure).

---

## Section 8: Exercises

To master this, you need to fix the engine yourself. Complete these exercises in order:

1. **Thought exercise**: Trace the timeline above. Draw it out on paper. Identify the exact line of code where each thread is stuck when the program hangs.
2. **Fix the Parser Starvation**: Open `parser.cpp`. Minimize the lock scope in `Parser::Run()`. Lock only for the `pop()` call. For now, add `std::this_thread::yield()` when the queue is empty to prevent busy-waiting. (In the next lesson, we'll upgrade the Parser to use a Condition Variable).
3. **Fix the Shutdown Sequence**: Open `lexer.cpp`. In `Lexer::Run()`, uncomment `running.store(false, std::memory_order_release)`. Immediately after, add `queueCV.notify_all()` to wake up any workers sleeping on an empty queue.
4. **Fix the Predicate**: In `Lexer::worker()`, update the `queueCV.wait` predicate so that it wakes up if `!running.load()`.
5. **Understand Notify Placement**: Move `queueCV.notify_one()` outside the `lock_guard` scope in `Lexer::Run()`. Add a comment explaining *why* this prevents the "hurry up and wait" anti-pattern.
6. **Bonus — The RingBuffer Safety Question**: Your `lineQueue` ring buffer is used with a mutex. Your `dirQueue` is used WITHOUT a mutex. Explain in your own words why `dirQueue` is safe without one and `lineQueue` is not. What exact sequence of events would corrupt `lineQueue` if you removed `lineQueueMutex`?

Welcome to Systems Engineering. It's tough, but once you fix these locks, your engine will absolutely scream across all CPU cores.

In the next lesson, we will look at advanced thread coordination and how to build a fully lock-free pipeline.
