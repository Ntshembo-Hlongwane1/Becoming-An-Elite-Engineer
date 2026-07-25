# Concurrency: Escaping the Event Loop

Ntshembo, welcome to the hardest part of systems engineering.

Up until now, we've talked about memory, pointers, cache lines, and making a single thread run as fast as humanly possible. We've optimized allocations, avoided unnecessary copies, and laid out data sequentially to keep the CPU fed.

But modern CPUs don't just have one core. My machine has 16. Yours probably has at least 8. If you only use one core, your program is leaving 80-90% of the hardware's potential sitting completely idle.

To tap into that power, we need concurrency. We need multiple threads working simultaneously.

However, moving from a single-threaded world to a multi-threaded world is not just a syntax change. It is a fundamental shift in how you reason about state, time, and causality.

In this lesson series, we are going to dive deep into true parallelism in C++. We'll look at the concurrency primitives, the architectural patterns for building concurrent pipelines, and the nasty bugs you're currently fighting in your search engine.

Let's get started.

---

## Part 1: Where You Are (The Node.js Mental Model)

Before we look at C++, we need to deconstruct the mental model you've built over years of writing Node.js.

Node.js is famous for being "asynchronous" and "non-blocking." It handles thousands of concurrent network connections beautifully. Because of this, it's easy to assume Node.js is doing things in parallel.

It is not.

**Node's event loop is STRICTLY single-threaded.**

There is exactly *one* call stack executing your JavaScript code. When you write:

```javascript
async function processData() {
    const data1 = await fetchA();
    const data2 = await fetchB();
    let result = compute(data1, data2);
    return result;
}
```

The `await` keyword is essentially a yield point. It says, "Pause this function, put it aside, and let the event loop run other JavaScript code until `fetchA` is done."

This is **cooperative multitasking**, not parallelism.

When you do `Promise.all([fetchA(), fetchB()])`, the network requests overlap in time because they are I/O bound. The operating system handles the actual network traffic in the background. But when the data comes back, the callbacks that process `fetchA` and `fetchB` will *never* run at the exact same nanosecond. The event loop queues them up and executes them sequentially.

### The Node.js Safety Net

This single-threaded nature gives you an incredible superpower that you probably don't even realize you have: **You never have to worry about data races.**

Consider this JavaScript code:

```javascript
let counter = 0;

async function increment() {
    let current = counter;
    await someAsyncThing();
    counter = current + 1;
}

increment();
increment();
```

In Node.js, even though there's an `await` in the middle, while the synchronous parts of `increment()` are running, *no other JavaScript code can run*. Your JavaScript execution cannot be interrupted halfway through a synchronous block to run another function on a different thread. (The logic bug above is a race condition across asynchronous boundaries, but it's not a *data race* at the memory level).

Because your JavaScript code never runs on two CPU cores at the same time:
- You never face low-level data races (two threads writing to the exact same memory address simultaneously).
- You never have to use mutexes to protect shared variables.
- You never write code that deadlocks because of lock acquisition order.

> **The Key Insight:** In Node.js, the runtime protects you from concurrency bugs. The event loop acts as a global lock that serializes all your logic. In C++, *you* are the runtime.

*(Note: Yes, libuv, the C library underneath Node, uses a thread pool internally for things like `fs` operations, DNS resolution, and crypto. But it completely hides the synchronization and threading from your userland JavaScript code).*

---

## Part 2: What Changes in C++ (True Parallelism)

When you write multi-threaded C++ code, you are asking the Operating System for actual, physical OS threads. The OS scheduler will take these threads and assign them to separate CPU cores.

**These threads will run at the exact same time.**

If Thread A and Thread B both call the same function, they are executing those instructions simultaneously.

This means if Thread A and Thread B both have a pointer to the same variable, and they both try to modify it at the exact same nanosecond... the universe breaks.

Let's visualize the difference.

### The Node.js Timeline (Time-Slicing)

```text
Time --->
Core 1:  [Task A] ---> [Task B] ---> [Task A resumes] ---> [Task C]
Core 2:  (Idle)
Core 3:  (Idle)
```
*In Node.js, tasks take turns on the single core. Shared state is safe because only one task touches it at any given moment.*

### The C++ Timeline (True Parallelism)

```text
Time --->
Core 1:  [Thread 1: process_chunk_1()] --------------------->
                                     \
Core 2:  [Thread 2: process_chunk_2()] -----> [Shared Memory: g_TotalCount]
                                     /
Core 3:  [Thread 3: process_chunk_3()] --------------------->
```

*In C++, multiple threads are executing simultaneously on different cores. If they all try to update `g_TotalCount` at once, their reads and writes will interleave at the hardware level, corrupting the data.*

There is no event loop serializing access.
There is no garbage collector cleaning up if you drop a reference to a running thread.
There is no runtime safety net.

This is what **shared mutable state** means. If state is shared between threads, and it is mutable (can be changed), it is a ticking time bomb unless you explicitly synchronize access to it. It is universally considered the hardest problem in systems engineering.

---

## Part 3: The Concurrency Toolbox

To survive in this parallel world, you need tools to control how threads interact with shared memory and with each other. Here are the five fundamental primitives you'll be using.

We won't go deep into their implementation here—that's what the deep-dive docs are for—but you need to know what they are.

### 1. `std::mutex` (The Lock)
"Mutex" stands for "Mutual Exclusion." It is a lock.
Only one thread can hold the mutex at a time. If Thread A locks the mutex, and Thread B tries to lock it, Thread B will block (go to sleep) until Thread A unlocks it.
*Use it to protect shared data from simultaneous access.*

### 2. `std::lock_guard` / `std::unique_lock` (The RAII Wrappers)
You should almost never call `.lock()` and `.unlock()` manually on a mutex. If your code throws an exception or returns early before hitting `.unlock()`, the mutex stays locked forever, and your program freezes.
Instead, we use RAII wrappers like `std::lock_guard`. You create it on the stack, it locks the mutex in its constructor, and when it goes out of scope, its destructor automatically unlocks the mutex.
*Think of the destructor as an automatic `finally` block that is guaranteed to run.*

### 3. `std::condition_variable` (The Signaling Mechanism)
A condition variable allows threads to wait until a specific condition becomes true, and allows other threads to wake them up.
You use it when a thread needs to say, "Put me to sleep until there's work in the queue, then wake me up."
*Compare it to a Node.js `EventEmitter` emitting a "data" event, but designed to work safely across threads while tightly coupled to a mutex.*

### 4. `std::atomic` (The Lock-Free Variable)
Sometimes a mutex is too heavy just to increment a counter. `std::atomic<int>` asks the CPU hardware to guarantee that reads and writes to this integer are indivisible (atomic). If two threads increment it at the exact same time, the hardware ensures they both happen sequentially and safely, without using an OS-level lock.
*There is absolutely no equivalent in Node.js, because Node never has two threads touching a variable simultaneously.*

### 5. RingBuffer / Lock-Free Queues (The Channel)
Your `RingBuffer` implementation is a concurrent data structure. These structures act as communication channels between threads. One thread pushes data in, another pulls it out.
*Think of it like Go channels, or Node.js Streams with backpressure, but built explicitly for inter-thread communication.*

---

## Part 4: The Four Concurrency Bugs (The Symptom Map)

When you write single-threaded code, bugs usually result in wrong output or a crash.
When you write multi-threaded code, bugs result in your program hanging forever, pegging the CPU at 100% while doing nothing, or randomly corrupting data once every 10,000 runs.

There are four major categories of concurrency bugs. I have mapped them directly to the symptoms you are currently seeing in your search engine codebase.

### 1. Data Race
Two threads access the same memory location simultaneously, at least one is writing, and there is no synchronization (no mutex, no atomic).
**Result:** Undefined behavior. The program is fundamentally invalid. It might crash, it might compute the wrong answer, or it might appear to work perfectly on your machine and fail in production.

### 2. Deadlock
Thread A holds Lock X and is waiting for Lock Y.
Thread B holds Lock Y and is waiting for Lock X.
**Result:** Both threads wait forever. The program freezes instantly.

### 3. Livelock / Starvation
Threads are actively running (consuming CPU), but the system as a whole makes no progress. One thread might be aggressively acquiring a lock and releasing it, starving other threads from ever getting a chance to acquire it.
**Result:** The program grinds to a halt or appears frozen, but CPU usage remains high.

### 4. Termination Failure
The program works fine, but when it's time to shut down, it hangs. The main thread calls `.join()` on a worker thread, waiting for it to exit, but the worker thread is asleep waiting for a condition variable that will never be signaled.
**Result:** The app refuses to close and has to be force-killed.

### Your Actual Bugs Mapped

Here is what is currently happening in your code. We will fix every single one of these in the exercises.

| Symptom in YOUR code | Category | Root cause | File | Fix concept | Deep dive |
|---|---|---|---|---|---|
| Program freezes after indexing files | Starvation | `Parser::Run()` spin-holds `g_parserMutex`, lexer workers can't push tokens | [parser.cpp](file:///D:/Escape%20the%20Abstraction/searchengine/src/parser.cpp#L30-L40) | Minimize lock scope, use condition variable | `concurrency-approach1.md` |
| Program hangs on `thread.join()` forever | Termination Failure | `Lexer::Run()` exits without setting `running=false` or notifying workers | [lexer.cpp](file:///D:/Escape%20the%20Abstraction/searchengine/src/lexer.cpp#L156) | Proper shutdown signaling with atomics + CV | `concurrency-approach1.md` |
| Workers never wake up even if `running` set to false | Termination Failure | Wait predicate only checks `!lineQueue.empty()`, ignores `running` flag | [lexer.cpp](file:///D:/Escape%20the%20Abstraction/searchengine/src/lexer.cpp#L66) | Correct predicate design | `concurrency-approach1.md` |
| Workers spawned twice (in `Start()` and again in `main()`) | Design Error | `Lexer::Start()` spawns workers, then `main()` calls `Run()` on another thread | [lexer.cpp](file:///D:/Escape%20the%20Abstraction/searchengine/src/lexer.cpp#L26) & [main.cpp](file:///D:/Escape%20the%20Abstraction/searchengine/src/main.cpp#L82) | Clear ownership of thread lifecycle | `concurrency-approach2.md` |
| `notify_one()` called inside lock scope | Performance | Woken thread immediately blocks on the same mutex | [lexer.cpp](file:///D:/Escape%20the%20Abstraction/searchengine/src/lexer.cpp#L143) | Notify outside lock scope | `concurrency-approach1.md` |

> **Take a close look at that table.** The bugs you are fighting right now aren't random flukes. They are classic concurrency patterns. Starvation, bad predicates, unclear ownership, lock contention. By naming them, we can conquer them.

---

## Part 5: How to Read the Deep-Dive Docs

I have prepared two massive deep-dive documents that will guide you through fixing your search engine. You must read them in order.

| Doc | Focus | The Question It Answers |
|---|---|---|
| `concurrency-approach1.md` | **Synchronization primitives** | How do mutexes, condition variables, and atomics actually work? How do you use them without deadlocking, starving, or leaking threads? |
| `concurrency-approach2.md` | **Pipeline architecture** | How do you design a multi-stage concurrent pipeline (producer → worker pool → consumer) that starts cleanly, communicates safely, and shuts down gracefully? |

Think of it like this:
**`concurrency-approach1.md` is the THEORY.** It teaches you how the bricks and mortar work. It explains *why* calling `notify_one()` while holding a lock is bad for performance, and *why* your wait predicate must include the shutdown flag.
**`concurrency-approach2.md` is the ARCHITECTURE.** It teaches you how to build the house. It shows you how to wire your Lexer and Parser together so that the main thread owns the lifecycle, starts everything up cleanly, and shuts it all down predictably.

---

## Part 6: Exercises

Your mission is to completely overhaul the concurrency model of the search engine. Follow these steps exactly:

1. **Read `concurrency-approach1.md`** and complete its exercises first. You need to intimately understand the primitives before you try to wire the pipeline.
2. **Read `concurrency-approach2.md`** and complete its exercises. This will involve redesigning the architecture of `Lexer` and `Parser`.
3. **Apply the fixes to your actual code.** Go into `lexer.cpp`, `parser.cpp`, and `main.cpp`. Strip out the old broken spin-locks and confusing double-spawns. Implement clean condition variables, atomics, and RAII locks.
4. **Add diagnostic logging.** Before you run the new code, add `std::cout` (or your logger) statements at key lifecycle moments: thread start, thread processing, thread wake-up, and thread exit. You need to *see* the threads behaving correctly in the logs.
5. **Stress Test.** Run the engine against a large directory of files. Verify that it parses everything quickly, doesn't freeze in the middle, and cleanly exits when it's done without hanging on `.join()`.

You are stepping out of the comfortable sandbox of the JavaScript event loop and into the raw power of the operating system. It's going to be frustrating at times. You will see hangs. You will see deadlocks.

But when you get it right—when you watch your search engine ingest thousands of files, utilizing 100% of every core on your machine, coordinating seamlessly and shutting down gracefully—there is no feeling quite like it.

Let's build. Start with `concurrency-approach1.md`.
