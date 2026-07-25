# Concurrency Deep Dive 2: Pipeline Architecture and Thread Lifecycle

Hey Ntshembo. Welcome to the second deep-dive in our concurrency series. 

In the first approach, we tackled the raw mechanics of locks, atomic operations, and how to avoid spinning your CPU into oblivion. We focused on the *micro* level of concurrency: what happens when two threads touch the exact same variable at the exact same time.

Today, we're zooming out to the *macro* level. We're going to look at the overall architecture of your system. 

When you were building in Node.js, you used Streams. You took a readable stream, piped it into a transform stream, and piped that into a writable stream. The data flowed, the memory stayed low, and the event loop magically handled the backpressure and the shutdown. 

You're trying to build the exact same thing in C++. Your search engine reads files, transforms them into tokens, and writes those tokens into a parser. It's a pipeline. But here's the catch: **in C++, there is no event loop to manage the flow for you.** You are the event loop. You are the orchestrator. If you don't explicitly design how threads start, communicate, and die, they will hang forever.

Let's dissect your pipeline, find out why your threads are currently deadlocking, and rebuild it into a robust, elite-level concurrent system.

---

## Section 1: What Is a Pipeline?

A concurrent pipeline is a series of **stages** connected by **queues**. 
- Each stage is an independent unit of work running on its own thread (or a pool of threads).
- Data flows from one stage to the next through thread-safe queues.
- As soon as Stage 1 finishes a chunk of data, it hands it off to Stage 2 and immediately starts working on the next chunk.

> **The Aha Moment**: The goal of a pipeline isn't just to do things simultaneously. It's to keep every CPU core fed. While the parser is crunching tokens from file A, the lexer is tokenizing file B, and the disk is reading file C. 

In Node.js, you would write this as:
```javascript
fs.createReadStream('data/')
  .pipe(new LexerTransform())
  .pipe(new ParserWritable());
```
Node handles the buffering and thread pool internally. 

In Unix, you write this as:
```bash
cat file | grep pattern | sort
```
The OS spins up three separate processes and connects their standard input/output using memory buffers (pipes). 

In your C++ search engine, you are building this exact architecture yourself using `std::thread` for the stages and your custom `RingBuffer` for the pipes.

Let's draw your actual pipeline from `main.cpp`:

```text
┌──────────────┐   dirQueue    ┌──────────────┐   lineQueue   ┌──────────┐   parserQueue   ┌──────────┐
│ DirReader    │──────────────→│ Lexer::Run() │──────────────→│ Workers  │────────────────→│ Parser   │
│ (producer)   │  RingBuffer   │ (file reader)│  RingBuffer   │ (tokenize)│   RingBuffer    │ (consumer)│
└──────────────┘               └──────────────┘               └──────────┘                └──────────┘
    1 thread                      1 thread                     2 threads                    1 thread
```

Data flows from left to right. But right now, the flow gets stuck. To understand why, we need to look at the three unbreakable laws of pipeline design.

---

## Section 2: The Three Laws of Pipeline Design

Every concurrent pipeline MUST solve three problems. If you skip any one of them, the pipeline breaks. It might look like it works for small datasets, but under load, it will crash, drop data, or deadlock.

### Law 1: Clean Startup
*All threads must be created and reach their "ready" state before data starts flowing.*

If a producer starts blasting data into a queue before the consumer thread is fully initialized, the queue might overflow. 

In your code, `kernal.StartAll()` calls `Lexer::Start()`, which spawns `worker1` and `worker2`. Then `main()` spawns the `lexer_thread`, `dir_reader_thread`, and `parser_thread`. 
Because your workers are spawned *before* `Lexer::Run()` and `DirectoryReader::Run()` start pumping data, you accidentally obey this law. But relying on the order of function calls in `main()` without explicit synchronization is brittle. We'll fix this in Section 5.

### Law 2: Safe Communication
*Every queue must be safe for its actual access pattern.*

Not all queues need heavy locks. It depends entirely on how many threads are pushing and popping.
- **SPSC (Single-Producer, Single-Consumer):** Only one thread pushes, only one pops. A lock-free ring buffer works perfectly here. No mutexes needed.
- **MPSC (Multi-Producer, Single-Consumer):** Multiple threads push, one pops. You need a mutex on the push side, or a specialized lock-free MPSC algorithm.
- **SPMC (Single-Producer, Multi-Consumer):** One pushes, multiple pop. You need a mutex on the pop side, or a specialized SPMC queue.
- **MPMC (Multi-Producer, Multi-Consumer):** Everyone pushes, everyone pops. You need full mutex protection on both sides.

Let's map out the queues in your current architecture:

| Queue | Producers | Consumers | Pattern | Current protection | Correct? |
|---|---|---|---|---|---|
| `dirQueue` | `DirectoryReader::Run` (1) | `Lexer::Run` (1) | **SPSC** | None | ✅ Yes. (Assuming your RingBuffer is designed for lock-free SPSC) |
| `lineQueue` | `Lexer::Run` (1) | `worker1`, `worker2` (2) | **SPMC** | `lineQueueMutex` + CV | ✅ Yes. Multiple consumers need synchronization to avoid popping the same item. |
| `parserQueue` | `worker1`, `worker2` (2) | `Parser::Run` (1) | **MPSC** | `g_parserMutex` | ⚠️ Mutex needed, but your current usage spins instead of sleeping. |

### Law 3: Graceful Shutdown
*Every thread must have a definitive, unbreakable way to know "we are done, pack it up."*

In Node.js, when the `fs.createReadStream` reaches EOF, it emits an `'end'` event. This event cascades down the `.pipe()` chain. Every transform stream flushes its buffers, closes, and passes the `'end'` event downstream.

In C++, you must build this cascade yourself. The standard pattern is the **POISON PILL**.

A poison pill is a special sentinel value that you push into the queue. It tells the consumer: *"This isn't real data. This means the producer is dead, and no more data is coming. Finish what you have and die."*

Each stage, when it finishes its own work, MUST send a poison pill downstream. The chain looks like this:
1. `DirReader` finishes reading the directory → sends poison pill to `dirQueue`
2. `Lexer::Run` sees it, finishes its files → sends poison pills to `lineQueue` (one for each worker)
3. `Workers` see them, finish tokenizing → send poison pills to `parserQueue`
4. `Parser` sees it, finishes parsing → exits gracefully.

**This is where your code currently fails completely.** Let's dive deep into the autopsy of your shutdown sequence.

---

## Section 3: The Shutdown Problem in Detail

Let's walk through your codebase line by line and trace exactly why your program hangs on the `join()` calls at the end of `main()`.

### Stage 1: DirectoryReader::Run()
Inside your `DirectoryReader`, you iterate through the `data/` directory and push file paths into `dirQueue`. When the loop finishes, the function simply returns. 

**Fatal Flaw:** It does NOT push a poison pill to `dirQueue`.

Now look at `Lexer::Run()`:
```cpp
// In Lexer::Run()
while (true) {
    std::string file;
    if (dirQueue.pop(file)) {
        if (file == "") { // <--- Waiting for the poison pill!
            break;
        }
        // ... process file ...
    }
}
```
You *designed* the lexer to look for an empty string `""` as the poison pill. But who pushes that empty string? Nobody! 
`DirectoryReader` just goes home. `Lexer::Run()` sits in a `while(true)` loop forever. `dirQueue.pop(file)` returns `false` (because the queue is empty), so the inner `if` is skipped, and the CPU spins at 100% doing absolutely nothing.

### Stage 2: Lexer::Run()
Let's pretend `DirectoryReader` *did* send the poison pill. `Lexer::Run()` would break out of its loop. What happens next?

```cpp
// In Lexer::Run(), after the while loop
// running.store(false);  <--- THIS IS COMMENTED OUT!
// queueCV.notify_all();
```

**Fatal Flaw:** Even if `Lexer::Run()` exits, it never tells the worker threads to stop. The workers are blocked on `queueCV.wait()`, expecting more lines of text. Without `running = false` and `notify_all()`, they will sleep until the end of time.

### Stage 3: The Workers
Let's pretend `Lexer::Run()` *did* set `running = false` and notified the condition variable.
```cpp
// In Lexer::worker()
while (true) {
    ILP item;
    {
        std::unique_lock<std::mutex> lock(lineQueueMutex);
        queueCV.wait(lock, [this] { return !lineQueue.empty(); }); 
        // ... pop item ...
    }
}
```

**Fatal Flaw:** When `notify_all()` wakes up the worker, the condition variable checks its predicate: `!lineQueue.empty()`. 
If the queue is empty (because all lines are processed), the predicate returns `false`. The condition variable thinks it was a spurious wakeup, and goes right back to sleep! It doesn't check the `running` flag. Even if you set it, the workers ignore it.

### Stage 4: Parser::Run()
Finally, what about the parser?
```cpp
// In Parser::Run()
while (true) {
    std::string token;
    // ... pop token ...
    if (token == "") {
        break; // Poison pill!
    }
}
```
The parser is also waiting for an empty string. But the workers never push an empty string when they die. So the parser will also spin forever.

### The Cascade of Failure

Here is the architectural reality of what is happening in your code versus what you intended:

```text
WHAT YOU DESIGNED TO HAPPEN:                WHAT ACTUALLY HAPPENS:
DirReader done                              DirReader done
  → push "" to dirQueue                       → just returns
  → Lexer::Run sees ""                        → Lexer::Run spins forever 💥
  → Lexer sets running=false                  → (never reached)
  → Lexer notifies workers                    → (never reached)
  → Workers wake, see running=false            → Workers sleep forever 💥
  → Workers push "" to parserQueue             → (never reached)
  → Parser sees "", breaks                     → Parser spins forever 💥
  → All threads exit                          → main() hangs on join()
  → main() reaches "Press Enter"              → DEADLOCK 💀
```

This is why concurrency is unforgiving. One missing message at the start of the pipeline paralyzes the entire system.

---

## Section 4: Designing a Correct Shutdown

To fix this, we need to guarantee that the termination signal propagates cleanly down the pipeline. You can use the **Poison Pill Pattern**, the **Atomic Flag Pattern**, or a combination of both.

### The Poison Pill Pattern (Data-Driven Shutdown)

In this pattern, termination is treated as just another piece of data flowing through the queue.

> **The Rule of Poison Pills**: A producer must push exactly N poison pills, where N is the number of consumer threads listening to that queue.

1. **DirectoryReader to Lexer::Run (1 Consumer)**
   ```cpp
   // At the end of DirectoryReader::Run()
   dirQueue.push(""); // Send the poison pill
   ```

2. **Lexer::Run to Workers (2 Consumers)**
   The workers need a way to know they are done. You can push an empty `ILP` struct. Because there are 2 workers, you must push 2 poison pills!
   ```cpp
   // At the end of Lexer::Run()
   lineQueue.push({"", ""}); // Poison pill for worker 1
   lineQueue.push({"", ""}); // Poison pill for worker 2
   queueCV.notify_all();     // Wake them up so they consume the pills
   ```

3. **Workers to Parser (1 Consumer)**
   When a worker pops the empty `ILP`, it knows it's time to die. Before it dies, it must notify the parser.
   Wait, if we have 2 workers, and they both push a poison pill to the parser, the parser will get 2 pills. Is that okay?
   If the parser exits on the first pill, the second worker might block trying to push its pill. 
   
   *Alternative approach for MPSC queues:* Use an atomic counter.
   ```cpp
   // Global or shared atomic
   std::atomic<int> active_workers{2};
   
   // In Lexer::worker()
   if (item.filename == "" && item.line == "") {
       // I'm dying. Am I the last one?
       if (active_workers.fetch_sub(1) == 1) { 
           // fetch_sub returns the PREVIOUS value. If it was 1, I am the last one alive!
           parserQueue.push(""); // Only the last worker sends the poison pill to the Parser
       }
       break; 
   }
   ```

### The Atomic + CV Pattern (State-Driven Shutdown)

Instead of pushing empty structs, you use shared atomic booleans to broadcast the shutdown state. This is cleaner when condition variables are involved.

```cpp
// In Lexer::worker()
while (true) {
    ILP item;
    {
        std::unique_lock<std::mutex> lock(lineQueueMutex);
        // We wake up if there is data OR if we are no longer running
        queueCV.wait(lock, [this] { return !lineQueue.empty() || !running.load(); }); 
        
        if (lineQueue.empty() && !running.load()) {
            // No more data, and the boss said stop. Time to die.
            break; 
        }
        
        // ... pop and process item ...
    }
}
```

This is much more resilient. If `Lexer::Run()` finishes, it simply does:
```cpp
running.store(false);
queueCV.notify_all(); // Wake up all sleeping workers to see the new running state
```
You don't have to worry about pushing exactly N poison pills.

**Recommendation:** Use Poison Pills for lock-free queues (`dirQueue`, `parserQueue`), and use the Atomic + CV pattern for queues that already use condition variables (`lineQueue`).

---

## Section 5: Thread Ownership — Who Creates, Who Joins?

Look at how threads are spawned in your codebase:
```cpp
// Inside Lexer::Start()
worker1 = std::thread(&Lexer::worker, this, "1"); 
worker2 = std::thread(&Lexer::worker, this, "2"); 

// Inside main()
std::thread lexer_thread(&Lexer::Run, lx);
```

Who is responsible for `worker1` and `worker2`? `main()` joins `lexer_thread`, but NOBODY joins `worker1` and `worker2`. 

> **The C++ Thread Contract**: If you create a `std::thread`, you MUST call `.join()` or `.detach()` on it before the thread object is destroyed. If a `std::thread` destructor runs while the thread is still "joinable", the C++ runtime will immediately call `std::terminate()` and crash your entire program.

If you fix the deadlocks, your program will gracefully finish processing, and then violently crash when the `Lexer` object is destroyed because the worker threads were never joined.

**The Solution: Encapsulated Ownership**
Whoever creates the threads must own their lifecycle. If `Lexer` spawns workers, `Lexer` must join them.

### Design A: The Subsystem Manager (Recommended)
Hide the threading details from `main()`. Let `Lexer` manage its own concurrency.

```cpp
class Lexer {
    std::thread reader_thread;
    std::vector<std::thread> workers;
    std::atomic<bool> running{true};
    
public:
    void Start() {
        // Spawns everything related to Lexing
        reader_thread = std::thread(&Lexer::Run, this);
        workers.push_back(std::thread(&Lexer::worker, this, "1"));
        workers.push_back(std::thread(&Lexer::worker, this, "2"));
    }
    
    void Stop() {
        // This is called when we know no more directories are coming.
        // Or it's triggered internally when Run() finishes.
        
        if (reader_thread.joinable()) reader_thread.join();
        
        // Workers should already be notified by Run() finishing
        for (auto& w : workers) {
            if (w.joinable()) w.join();
        }
    }
};
```
In this design, `main()` doesn't touch `std::thread` for the lexer at all. It just says `lx->Start()` and eventually `lx->Stop()`. This is clean, modular deep systems engineering.

---

## Section 6: Backpressure — When the Producer Is Faster Than the Consumer

Let's look at a silent killer in your code. Look at `Lexer::Run()`:
```cpp
// In Lexer::Run()
while (std::getline(fileStream, line)) {
    lineQueue.push({file, std::string(line)}); // <--- DANGER!
    queueCV.notify_one();
}
```

Your `RingBuffer` has a fixed size (1024). What happens when `lineQueue` is full? 
Your `push()` method returns `false`. But you aren't checking the return value! 
If the lexer reads lines faster than the 2 workers can tokenize them (which is highly likely, disk I/O to RAM is faster than heavy string manipulation), the queue fills up. The next `push()` fails, returns `false`, and **THE LINE OF TEXT IS SILENTLY DELETED FROM EXISTENCE.**

You are dropping data under load.

In Node.js streams, this is called **Backpressure**. When a writable stream is full, `write()` returns `false`. Node.js automatically pauses the readable stream until the writable stream emits a `'drain'` event.

In C++, you must implement backpressure yourself. If the queue is full, the producer must WAIT.

**The simple fix (Spin-Wait):**
```cpp
while (!lineQueue.push({file, std::string(line)})) {
    // Queue is full. Yield the CPU to let consumers catch up.
    std::this_thread::yield(); 
}
queueCV.notify_one();
```

**The elite fix (Condition Variables):**
Just as consumers wait on a CV when the queue is *empty*, producers should wait on a CV when the queue is *full*.
```cpp
// In Lexer::Run()
std::unique_lock<std::mutex> lock(lineQueueMutex);
// Wait until there is space in the queue
notFullCV.wait(lock, [this] { return !lineQueue.is_full(); });

lineQueue.push({file, std::string(line)});
queueCV.notify_one(); // Wake up consumers
```
This prevents the producer from burning CPU cycles while waiting for space.

---

## Section 7: The Complete Corrected Pipeline Architecture

If we apply all these fixes—Poison Pills, State Flags, Thread Ownership, and Backpressure—your system architecture transforms from a fragile cascade into a robust, industrial-grade pipeline:

```text
┌──────────────┐   dirQueue    ┌──────────────┐   lineQueue   ┌──────────┐   parserQueue   ┌──────────┐
│ DirReader    │──────────────→│ Lexer::Run() │──────────────→│ Workers  │────────────────→│ Parser   │
│              │  SPSC, no     │              │  SPMC, mutex  │          │   MPSC, mutex   │          │
│              │  lock needed  │              │  + CV         │          │   + CV          │          │
└──────┬───────┘               └──────┬───────┘               └────┬─────┘                └──────────┘
       │                              │                            │
       │ on finish:                   │ on "" received:            │ on atomic running=false:
       │ push "" to dirQueue          │ set running=false          │ decrement atomic counter
       │                              │ notify_all(workers)        │ if counter == 0:
       │                              │                            │    push "" to parserQueue
```

This pipeline will process 10 files or 10,000 files with equal stability. It will maximize CPU usage without spinning, and it will shut down gracefully, freeing all resources when the work is done.

---

## Section 8: Exercises

To truly escape the abstraction, you need to feel the pain of fixing this in the code. Complete these exercises in order:

1. **Map the Access Pattern:**
   Open your codebase. For `dirQueue`, `lineQueue`, and `parserQueue`, list every single function that calls `push()` and every function that calls `pop()`. Verify my assumptions about SPSC / SPMC / MPSC. 

2. **Fix the Data Drops (Backpressure):**
   Wrap every `push()` call in your entire codebase with a `while(!queue.push(...)) { std::this_thread::yield(); }` loop. Log a `std::cout` warning every time it loops. Run the engine on a large dataset and watch how often your threads were previously dropping data.

3. **Implement the Shutdown Chain:**
   - Modify `DirectoryReader` to push `""` when done.
   - Modify `Lexer::Run` to break on `""`, then set `running.store(false)` and `queueCV.notify_all()`.
   - Modify `Lexer::worker`'s condition variable predicate to check `!running.load()`.
   - Use an `std::atomic<int> active_workers` to have the LAST surviving worker push `""` to the `parserQueue`.

4. **Refactor Thread Ownership:**
   Move `worker1.join()` and `worker2.join()` into a `Lexer::Stop()` method. Ensure they are joined before the program exits to prevent `std::terminate` crashes.

5. **Thought Exercise: The Mutex Purge**
   Look at `lineQueueMutex` and `g_parserMutex`. What would happen if you deleted them right now? 
   Be extremely precise. Don't say "data corruption." Trace the execution:
   *If worker 1 and worker 2 call `lineQueue.pop()` at the exact same microsecond, what happens to the internal `head` and `tail` pointers of the RingBuffer? Do they read the same line? Does the pointer jump by 2? Write out the exact failure scenario.*

You are no longer writing scripts, Ntshembo. You are engineering systems. Take your time, draw it out on paper if you need to, and conquer the pipeline.
