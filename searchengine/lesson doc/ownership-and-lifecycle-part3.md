# Ownership & Lifecycle — Part 3: Subsystem-Owned Workers

## Where We Left Off

Part 1 taught you the mental model: ownership, RAII, control planes.  
Part 2 taught you the control plane pattern: how the Kernal orchestrates lifecycle.

Now we go deep into the hardest part: **how each subsystem manages its own worker threads**.

This is where your code is currently broken. Let's fix it by understanding the patterns.

---

## The Worker Pattern

A "worker" is a thread that sits in a loop, pulling work items from a queue, processing them, and repeating. It's the most common pattern in systems programming.

```
                    ┌─────────────┐
                    │   Queue     │
                    │  ┌───┐     │
    Producer ──────→│  │ A │     │
                    │  │ B │     │────→ Worker Thread
                    │  │ C │     │       (processes items)
                    │  └───┘     │
                    └─────────────┘
```

Your Lexer already has this pattern:

```cpp
void Lexer::worker(std::string thread) {
    while (running.load()) {
        ILP line;
        lineQueue.pop_blocking(line);  // Pull from queue
        // ... process the line ...     // Do work
    }
}
```

The challenge is: **how do you cleanly shut this down?**

---

## Anatomy of a Worker Thread

Every worker thread has four phases:

### Phase 1: Enter the Loop

```cpp
void Lexer::worker(std::string id) {
    std::cout << "[Worker " << id << "] Started" << std::endl;
```

This runs once when the thread starts. Use it for per-thread initialization (e.g., allocating thread-local buffers).

### Phase 2: Wait for Work

```cpp
    while (running_.load(std::memory_order_acquire)) {
        ILP line;
        lineQueue_.pop_blocking(line);
```

This is where the thread spends most of its time — either:
- Processing a work item, or
- Sleeping inside `pop_blocking()` waiting for the next one

### Phase 3: Check for Shutdown Signal

```cpp
        // Check for poison pill
        if (line.filepath.empty() && line.line.empty()) {
            std::cout << "[Worker " << id << "] Received shutdown signal" << std::endl;
            break;
        }
```

After waking up from `pop_blocking()`, the thread must check if it woke up because of real work or because of a shutdown signal.

### Phase 4: Exit Cleanup

```cpp
    }  // end while loop
    
    std::cout << "[Worker " << id << "] Exited cleanly" << std::endl;
}
```

Any per-thread cleanup happens here. The thread function returns, and the thread exits.

### The Complete Worker

```cpp
void Lexer::worker(std::string id) {
    // Phase 1: Enter
    std::cout << "[Worker " << id << "] Started" << std::endl;
    
    // Phase 2 & 3: Loop
    while (running_.load(std::memory_order_acquire)) {
        ILP line;
        lineQueue_.pop_blocking(line);
        
        // Phase 3: Check shutdown
        if (line.filepath.empty() && line.line.empty()) {
            break;
        }
        
        // Phase 2: Do work
        auto tokens = splitLine(line.line);
        for (auto& token : tokens) {
            // ... process token ...
        }
    }
    
    // Phase 4: Exit
    std::cout << "[Worker " << id << "] Exited cleanly" << std::endl;
}
```

---

## Your Subsystems: A Case-by-Case Analysis

Let's go through each subsystem in your SonarSearch engine and understand what threads it needs, how to start them, and how to stop them.

### Subsystem 1: Store — No Threads

The Store is a passive data structure. It doesn't have its own threads — other subsystems read from and write to it.

```cpp
Error Store::OnStart() {
    // Nothing to do — Store is passive
    return Error("");
}

Error Store::OnStop() {
    // Nothing to do — no threads to join
    // Could flush data to disk here if we were persistent
    return Error("");
}
```

**Ownership**: Store owns its data (`dataFiles_`, `searchIndex_`). Other subsystems borrow pointers to Store. When Store is destroyed, the data is gone — so make sure all subsystems that use Store are stopped first.

This is why your registration order matters:
```cpp
kernal.Register("Store", new Store());        // First to start, LAST to stop
kernal.Register("Engine", new Engine(store));  // Last to start, FIRST to stop
```

### Subsystem 2: DirectoryReader — One Thread

The DirectoryReader scans a directory and pushes file paths into a queue. It does a finite amount of work and then stops.

```cpp
class DirectoryReader : public Subsystem {
private:
    RingBuffer<std::string, 1024>& dirQueue_;
    std::thread run_thread_;
    std::atomic<bool> running_{false};
```

**Start**: Create a thread that calls `Run()`.

```cpp
Error DirectoryReader::OnStart() {
    running_.store(true);
    run_thread_ = std::thread(&DirectoryReader::Run, this);
    return Error("");
}
```

**Run**: Scan the directory, push files, send poison pill, exit.

```cpp
void DirectoryReader::Run() {
    std::string dataPath = "data";
    
    for (const auto& entry : std::filesystem::directory_iterator(dataPath)) {
        // Check if we should stop (in case shutdown happens during scan)
        if (!running_.load(std::memory_order_acquire)) {
            break;
        }
        
        if (entry.is_regular_file()) {
            dirQueue_.push_blocking(entry.path().string());
        }
    }
    
    // Signal to downstream that we're done
    dirQueue_.push_blocking(std::string(""));  // Poison pill
}
```

**Stop**: Signal and join.

```cpp
Error DirectoryReader::OnStop() {
    running_.store(false, std::memory_order_release);
    
    // The Run thread might be blocked on push_blocking if the queue is full.
    // In practice, this is unlikely for directory scanning, but in general
    // you'd want shutdown-aware queues here too.
    
    if (run_thread_.joinable()) {
        run_thread_.join();
    }
    
    return Error("");
}
```

**Key insight**: The DirectoryReader is a **finite producer**. It does work, finishes, and exits naturally. The poison pill it sends is for the Lexer's benefit, not its own shutdown. During a graceful shutdown, the `running_` flag lets us abort the directory scan early.

### Subsystem 3: Lexer — One Coordinator + Multiple Workers

The Lexer is the most complex subsystem. It has:
- A **Run thread** (coordinator) that reads files and feeds the line queue
- **Worker threads** that process lines from the line queue

```
     dirQueue                lineQueue                  parserQueue
        │                       │                           │
        ▼                       │                           ▼
  ┌──────────┐                  │                    ┌──────────┐
  │ Run()    │ ──reads files──→ │ ←── pop_blocking ──│ Worker 1 │──→ push tokens
  │ (coord.) │   pushes lines   │                    └──────────┘
  └──────────┘                  │                    ┌──────────┐
                                │ ←── pop_blocking ──│ Worker 2 │──→ push tokens
                                │                    └──────────┘
```

This is a **fan-out** pattern: one coordinator feeds multiple workers.

**Start**: Create all three threads.

```cpp
Error Lexer::OnStart() {
    running_.store(true, std::memory_order_release);
    
    // Start workers FIRST (they need to be ready when Run starts pushing data)
    worker1_ = std::thread(&Lexer::worker, this, "1");
    worker2_ = std::thread(&Lexer::worker, this, "2");
    
    // Then start the coordinator
    run_thread_ = std::thread(&Lexer::Run, this);
    
    return Error("");
}
```

**Why start workers before the coordinator?** Because the coordinator pushes data into the line queue immediately. If the workers aren't running yet, the queue fills up and the coordinator blocks on `push_blocking()`. This is a race condition that could cause startup to hang.

**Stop**: This is the complex part. Let's think through it step by step.

```
Stop() called
    │
    ▼
Set running_ = false
    │
    ├── Run thread might be blocked on dirQueue_.pop_blocking()
    │   → Need to unblock it (poison pill or shutdown-aware queue)
    │
    ├── Worker 1 might be blocked on lineQueue_.pop_blocking()
    │   → Need to unblock it (poison pill)
    │
    └── Worker 2 might be blocked on lineQueue_.pop_blocking()
        → Need to unblock it (poison pill)
```

```cpp
Error Lexer::OnStop() {
    // 1. Signal all threads
    running_.store(false, std::memory_order_release);
    
    // 2. Unblock the Run thread if it's waiting on dirQueue
    //    (In normal operation, DirectoryReader sends a poison pill when done.
    //     But during forced shutdown, it might not have been sent yet.)
    dirQueue_.push_blocking(std::string(""));  // Poison pill for Run thread
    
    // 3. Unblock worker threads
    //    Each worker needs its own poison pill because pop_blocking
    //    only wakes up ONE waiting thread per item pushed.
    lineQueue_.push_blocking(ILP{"", ""});  // For worker 1
    lineQueue_.push_blocking(ILP{"", ""});  // For worker 2
    
    // 4. Join the coordinator first, then workers
    //    (Coordinator might push more items while stopping, 
    //     which workers need to drain)
    if (run_thread_.joinable()) run_thread_.join();
    if (worker1_.joinable()) worker1_.join();
    if (worker2_.joinable()) worker2_.join();
    
    // 5. Send poison pill downstream to Parser
    parserQueue_.push_blocking(std::string(""));
    
    return Error("");
}
```

**Critical detail**: You need one poison pill per worker thread. If you have 2 workers blocked on `pop_blocking()`, you need to push 2 poison pills. One wakes up worker 1, the other wakes up worker 2.

### Subsystem 4: Parser — One Thread

The Parser is a simple consumer:

```cpp
Error Parser::OnStart() {
    running_.store(true);
    run_thread_ = std::thread(&Parser::Run, this);
    return Error("");
}

Error Parser::OnStop() {
    running_.store(false, std::memory_order_release);
    
    // Send poison pill in case the thread is blocked on pop
    parserQueue_.push_blocking(std::string(""));
    
    if (run_thread_.joinable()) {
        run_thread_.join();
    }
    
    return Error("");
}
```

### Subsystem 5: Engine — One Thread (or main thread)

The Engine handles user queries. You might run it on the main thread (blocking) or on its own thread:

```cpp
Error Engine::OnStart() {
    running_.store(true);
    // Run on its own thread so the control plane isn't blocked
    run_thread_ = std::thread(&Engine::Run, this);
    return Error("");
}

Error Engine::OnStop() {
    running_.store(false, std::memory_order_release);
    
    // If the Engine is blocking on std::cin.get(), this is tricky.
    // The user must press Enter, or we need platform-specific tricks.
    // For now, the simplest approach is: don't block on cin in the Engine.
    
    if (run_thread_.joinable()) {
        run_thread_.join();
    }
    
    return Error("");
}
```

---

## The Thread Lifecycle Diagram

Here's the complete picture of what happens during startup and shutdown:

### Startup (left to right)

```
main() calls kernal.StartAll()
    │
    ├── Store.Start()         → (no threads)
    │
    ├── DirReader.Start()     → spawns Run thread
    │                                │
    │                                └── scans directory, pushes to dirQueue
    │
    ├── Lexer.Start()         → spawns worker1, worker2, Run thread
    │                                │
    │                                ├── workers: pull from lineQueue, push to parserQueue
    │                                └── Run: pulls from dirQueue, reads files, pushes to lineQueue
    │
    ├── Parser.Start()        → spawns Run thread
    │                                │
    │                                └── pulls from parserQueue, processes tokens
    │
    └── Engine.Start()        → spawns Run thread
                                     │
                                     └── handles user queries
```

### Shutdown (right to left, reverse order)

```
main() calls kernal.StopAll()
    │
    ├── Engine.Stop()
    │     ├── sets running_ = false
    │     └── joins run_thread_
    │
    ├── Parser.Stop()
    │     ├── sets running_ = false
    │     ├── sends poison pill to parserQueue
    │     └── joins run_thread_
    │
    ├── Lexer.Stop()
    │     ├── sets running_ = false
    │     ├── sends poison pill to dirQueue (for Run thread)
    │     ├── sends 2 poison pills to lineQueue (for worker1, worker2)
    │     ├── joins run_thread_
    │     ├── joins worker1_
    │     ├── joins worker2_
    │     └── sends poison pill to parserQueue (for Parser)
    │
    ├── DirReader.Stop()
    │     ├── sets running_ = false
    │     └── joins run_thread_
    │
    └── Store.Stop()
          └── (nothing to do)
```

---

## Common Pitfalls and How to Avoid Them

### Pitfall 1: Forgetting to Join Threads

```cpp
// WRONG — thread is created but never joined
Error Lexer::Stop() {
    running_.store(false);
    return Error("");  // Workers are still running!
}

// RIGHT — always join
Error Lexer::OnStop() {
    running_.store(false);
    if (worker1_.joinable()) worker1_.join();
    if (worker2_.joinable()) worker2_.join();
    return Error("");
}
```

**Always call `.joinable()` before `.join()`.** A thread is not joinable if:
- It was default-constructed (never started)
- It was already joined
- It was detached

Calling `.join()` on a non-joinable thread throws `std::system_error`.

### Pitfall 2: Wrong Number of Poison Pills

```cpp
// WRONG — only one poison pill for two workers
lineQueue_.push_blocking(ILP{"", ""});
// One worker wakes up and exits.
// The other worker is STILL BLOCKED.
// join() hangs forever on the second worker.

// RIGHT — one per worker
lineQueue_.push_blocking(ILP{"", ""});  // for worker 1
lineQueue_.push_blocking(ILP{"", ""});  // for worker 2
```

**Rule**: One poison pill per consumer thread on that queue.

### Pitfall 3: Joining in the Wrong Order

```cpp
// DANGEROUS — coordinator might push more items after workers exit
if (worker1_.joinable()) worker1_.join();
if (worker2_.joinable()) worker2_.join();
if (run_thread_.joinable()) run_thread_.join();
// If workers exit first, the coordinator's push_blocking might
// block forever because nobody is draining the lineQueue.

// SAFER — join coordinator first
if (run_thread_.joinable()) run_thread_.join();
if (worker1_.joinable()) worker1_.join();
if (worker2_.joinable()) worker2_.join();
// Wait, but this has the same problem! If the coordinator is stuck
// on push_blocking because the queue is full, joining it first blocks forever.
```

The real solution is: make sure nothing can block indefinitely during shutdown. Shutdown-aware queues solve this because `push_blocking()` also returns false on shutdown.

For the poison pill approach, the order matters less because you're sending poison pills to unblock everyone before joining. The general principle: **unblock everything first, then join in any order**.

### Pitfall 4: Accessing Destroyed Objects After Threads Exit

```cpp
// DANGEROUS
Error Lexer::OnStop() {
    running_.store(false);
    // Workers might still be running and accessing lineQueue_...
    // If lineQueue_ is destroyed here, workers crash!
    return Error("");
}
// After OnStop returns, the Kernal might destroy the Lexer,
// which destroys lineQueue_ reference. But workers are still running.
```

**Rule**: All threads must be joined BEFORE the subsystem can be destroyed. `OnStop()` must guarantee that all threads have exited before it returns.

### Pitfall 5: The `running_` Flag Race Condition

```cpp
void Lexer::worker(std::string id) {
    while (running_.load(std::memory_order_acquire)) {
        ILP line;
        lineQueue_.pop_blocking(line);
        
        // SUBTLE BUG: running_ might be false here, but we still
        // popped a real item. We should process it before exiting.
        if (line.filepath.empty() && line.line.empty()) {
            break;
        }
        
        // Process the line — this is fine even if running_ is false
        processLine(line);
    }
}
```

In practice, this is usually fine. The `running_` flag prevents the worker from starting a new iteration. The poison pill is what actually causes the exit. The flag is a secondary check.

---

## Pattern: The Worker Pool

When you have multiple workers doing the same thing, wrap them in a pattern:

```cpp
class WorkerPool {
public:
    WorkerPool(size_t num_workers, std::function<void(std::string)> work_fn)
        : num_workers_(num_workers), work_fn_(work_fn) {}
    
    void Start() {
        running_.store(true);
        for (size_t i = 0; i < num_workers_; ++i) {
            workers_.emplace_back(work_fn_, std::to_string(i));
        }
    }
    
    void Stop() {
        running_.store(false);
        for (auto& w : workers_) {
            if (w.joinable()) w.join();
        }
        workers_.clear();
    }
    
    size_t Size() const { return num_workers_; }
    
private:
    size_t num_workers_;
    std::function<void(std::string)> work_fn_;
    std::vector<std::thread> workers_;
    std::atomic<bool> running_{false};
};
```

Now the Lexer becomes:

```cpp
class Lexer : public Subsystem {
    WorkerPool worker_pool_;
    
    Lexer(/*...*/) : worker_pool_(2, [this](std::string id) { worker(id); }) {}
    
    Error OnStart() {
        worker_pool_.Start();
        run_thread_ = std::thread(&Lexer::Run, this);
        return Error("");
    }
    
    Error OnStop() {
        // Send poison pills — one per worker
        for (size_t i = 0; i < worker_pool_.Size(); ++i) {
            lineQueue_.push_blocking(ILP{"", ""});
        }
        worker_pool_.Stop();
        // ...
    }
};
```

This scales. If you want 8 workers instead of 2, change one number.

---

## Comparing With What You Know: Express.js Middleware Chain

If you've used Express.js, you know middleware:

```javascript
app.use(logging);      // runs first
app.use(auth);         // runs second  
app.use(rateLimiter);  // runs third
app.use(handler);      // runs last
```

Your subsystem chain is the same concept:

```
DirectoryReader → Lexer → Parser → Engine
   (producer)    (transform)  (transform)  (consumer)
```

The key differences:

| Express Middleware | Your Subsystem Pipeline |
|---|---|
| Runs synchronously on request thread | Runs asynchronously on separate threads |
| Framework manages lifecycle | You manage lifecycle |
| No shutdown needed | Must gracefully shut down |
| Share request context | Share via queues (RingBuffer) |

The subsystem pipeline is like middleware, but concurrent and persistent. Each stage runs on its own thread(s), connected by queues.

---

## The Complete Thread Inventory

Here's every thread in your system and who owns it:

| Thread | Owner | Created In | Joined In | What It Does |
|--------|-------|------------|-----------|-------------|
| Main thread | OS/Runtime | Process start | Process end | Runs `main()`, waits for shutdown |
| DirReader::run_thread_ | DirectoryReader | `Start()` | `Stop()` | Scans directory, pushes file paths |
| Lexer::run_thread_ | Lexer | `Start()` | `Stop()` | Reads files, pushes lines to workers |
| Lexer::worker1_ | Lexer | `Start()` | `Stop()` | Tokenizes lines, pushes tokens |
| Lexer::worker2_ | Lexer | `Start()` | `Stop()` | Tokenizes lines, pushes tokens |
| Parser::run_thread_ | Parser | `Start()` | `Stop()` | Processes tokens |
| Engine::run_thread_ | Engine | `Start()` | `Stop()` | Handles user queries |

Total: 6 threads + main thread = 7 threads.

Every thread has exactly one owner. Every owner creates the thread in `Start()` and joins it in `Stop()`. No exceptions.

---

## Summary of Part 3

| Concept | Key Insight |
|---------|-------------|
| **Worker pattern** | Loop: pull from queue → check shutdown → process → repeat |
| **Poison pills** | One per consumer thread per queue |
| **Fan-out** | One coordinator feeds multiple workers via a shared queue |
| **Start order** | Start consumers before producers (workers before coordinator) |
| **Stop procedure** | Set flag → push poison pills → join coordinator → join workers |
| **Worker pool** | Wrap multiple identical workers in a reusable pattern |
| **Thread inventory** | Track every thread: who creates it, who joins it, what it does |

---

## What's Next

- **Part 4**: Putting it all together — The complete corrected implementation of your SonarSearch engine with proper lifecycle management
