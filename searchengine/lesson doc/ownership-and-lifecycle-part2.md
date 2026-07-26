# Ownership & Lifecycle — Part 2: The Control Plane Pattern

## Where We Left Off

Part 1 taught you:
- Your `Kernal` is a control plane
- Ownership means "who deletes this"
- Threads must be joined by their creator
- Graceful shutdown = Signal → Drain → Wait → Cleanup

Now let's build the actual control plane pattern. We'll redesign your `Kernal` to properly orchestrate startup, thread management, and graceful shutdown.

---

## The Problem Restated

Right now your startup sequence is scattered across two places:

**Inside the Kernal** (the control plane):
```cpp
kernal.InitAll();   // Calls Init() on each subsystem
kernal.StartAll();  // Calls Start() on each subsystem
```

**Inside main.cpp** (outside the control plane):
```cpp
// Manually creating threads — these should not be here
std::thread lexer_thread(&Lexer::Run, lx);
std::thread dir_reader_thread(&DirectoryReader::Run, dirReader);
std::thread parser_thread(&Parser::Run, parser);
engine->Run();

// Manually joining — these should not be here either  
lexer_thread.join();
dir_reader_thread.join();
parser_thread.join();
```

The whole point of a control plane is that `main.cpp` should be simple:

```cpp
int main() {
    Kernal kernal;
    
    // Register subsystems
    kernal.Register("Store", new Store());
    kernal.Register("Lexer", new Lexer(/* ... */));
    // ...

    // Control plane handles everything
    kernal.InitAll();
    kernal.StartAll();
    
    // Wait for shutdown signal
    kernal.WaitForShutdown();
    
    // Control plane handles cleanup
    kernal.StopAll();
    
    return 0;
}
```

That's it. `main()` should just wire things together and let the control plane handle the rest.

---

## Principle: Subsystems Own Their Own Threads

This is the key insight:

> **The Kernal does NOT create threads for subsystems. Each subsystem creates and manages its own threads.**

Why? Because each subsystem knows:
- How many threads it needs
- What those threads should do
- When those threads are done
- How to signal those threads to stop

The Kernal only knows:
- What subsystems exist
- What order to start them
- What order to stop them

### The Ownership Hierarchy

```
Kernal (control plane)
├── owns: Subsystem "Store"
│        └── no threads (passive data store)
│
├── owns: Subsystem "Dir Reader"  
│        └── owns: 1 thread (reads directories)
│
├── owns: Subsystem "Lexer"
│        └── owns: 1 Run thread + 2 worker threads
│
├── owns: Subsystem "Parser"
│        └── owns: 1 thread (processes tokens)
│
└── owns: Subsystem "Engine"
         └── owns: 1 thread (handles search queries)
```

**Ownership rule**: The arrows point DOWN. Each level owns and manages everything below it. The Kernal doesn't know or care about threads inside subsystems. It just says "Start" and "Stop".

---

## The Subsystem Lifecycle Contract

Every subsystem follows this contract:

### `Init()` — Allocate Resources, Validate Configuration

What happens here:
- Open configuration files
- Validate parameters
- Allocate buffers
- Establish connections
- Do everything EXCEPT start processing

What does NOT happen here:
- No threads are created
- No work is done
- No queues are drained

Think of this as the "pre-flight check" on an airplane. You check everything before you take off.

```cpp
Error Lexer::Init() {
    // Validate our queues are accessible
    // Pre-allocate any buffers we need
    // Load stop word lists
    // But do NOT start any threads
    return Error("");
}
```

**Why separate Init from Start?** Because Init can fail without consequences. If Init fails, you haven't started any threads, opened any ports, or modified any state. You can just report the error and exit cleanly. If Start fails, you might have threads running that need to be cleaned up.

### `Start()` — Begin Processing

What happens here:
- Create threads
- Begin consuming from queues
- Start accepting work

```cpp
Error Lexer::Start() {
    running_.store(true);
    
    // Create the Run thread (reads files, feeds line queue)
    run_thread_ = std::thread(&Lexer::Run, this);
    
    // Create worker threads (process lines from queue)
    worker1_ = std::thread(&Lexer::worker, this, "1");
    worker2_ = std::thread(&Lexer::worker, this, "2");
    
    return Error("");
}
```

### `Stop()` — Signal, Drain, Wait, Cleanup

This is the most complex method. It must:

1. **Signal** all threads to stop (set atomic flag)
2. **Unblock** any threads waiting on queues (send poison pills or notify condition variables)
3. **Wait** for all threads to finish (join)
4. **Cleanup** any resources (close files)

```cpp
Error Lexer::Stop() {
    // 1. SIGNAL: Tell all threads it's time to stop
    running_.store(false, std::memory_order_release);
    
    // 2. UNBLOCK: Wake up threads that might be blocking on queues
    //    Send poison pills so pop_blocking() returns
    lineQueue_.push_blocking(ILP{"", ""});  // for worker1
    lineQueue_.push_blocking(ILP{"", ""});  // for worker2
    
    // 3. WAIT: Join all threads
    if (worker1_.joinable()) worker1_.join();
    if (worker2_.joinable()) worker2_.join();
    if (run_thread_.joinable()) run_thread_.join();
    
    // 4. CLEANUP: Close any open files
    if (file_ && file_ != stdin) {
        fclose(file_);
        file_ = nullptr;
    }
    
    return Error("");
}
```

---

## Deep Dive: Why Threads Block and Why That Matters for Shutdown

This is where most people from an API background get tripped up.

### The Blocking Problem

Your workers do this:

```cpp
void Lexer::worker(std::string thread) {
    while (running.load(std::memory_order_acquire)) {
        ILP line;
        lineQueue.pop_blocking(line);  // ← THIS BLOCKS FOREVER
        // ...
    }
}
```

Setting `running = false` is not enough. Here's why:

```
Timeline:

Thread enters while loop → checks running → it's true
Thread calls pop_blocking() → queue is empty → thread SLEEPS
                                                    ↑
Main thread sets running = false ──────────────────│
                                                    │
Thread is STILL SLEEPING. It never checks running again.
Thread will sleep forever. join() will hang forever.
```

The thread is stuck inside `pop_blocking()`, waiting on a condition variable. Setting `running = false` does nothing because the thread is not checking `running` — it's sleeping.

### The Solution: Poison Pills

A "poison pill" is a special message that tells the consumer "there is no more work, exit your loop":

```cpp
// Before joining, send a poison pill for EACH worker
lineQueue.push_blocking(ILP{"", ""});  // Worker 1 will receive this and exit
lineQueue.push_blocking(ILP{"", ""});  // Worker 2 will receive this and exit
```

The worker checks for it:

```cpp
void Lexer::worker(std::string thread) {
    while (running.load(std::memory_order_acquire)) {
        ILP line;
        lineQueue.pop_blocking(line);
        
        // Check for poison pill
        if (line.filepath.empty() && line.line.empty()) {
            break;  // Exit the loop
        }
        
        // ... process the line ...
    }
}
```

**Key insight**: You already have this pattern in your code! Your `DirectoryReader::Run()` sends a poison pill:

```cpp
// directoryreader.cpp
dirQueue.push_blocking(std::string(""));  // poison pill for Lexer::Run()
```

And your `Lexer::Run()` checks for it:

```cpp
if (file.empty()) {
    break;  // Exit on poison pill
}
```

You understand the pattern intuitively. You just haven't applied it systematically to shutdown.

### Alternative: Shutdown-Aware Queue

Instead of poison pills, you can make your RingBuffer itself shutdown-aware:

```cpp
template<typename T, size_t SIZE>
class RingBuffer {
    std::atomic<bool> shutdown_{false};
    
public:
    void request_shutdown() {
        shutdown_.store(true, std::memory_order_release);
        not_empty.notify_all();  // Wake up all blocked consumers
        not_full.notify_all();   // Wake up all blocked producers
    }
    
    // Returns false if shutdown was requested
    bool pop_blocking(T& item) {
        std::unique_lock<std::mutex> lock(mtx);
        
        not_empty.wait(lock, [this] {
            if (shutdown_.load(std::memory_order_acquire)) return true;
            return read_index.load() < write_index.load();
        });
        
        // Check if we woke up due to shutdown
        if (shutdown_.load(std::memory_order_acquire)) {
            return false;  // Signal to caller: "we're shutting down"
        }
        
        // Normal pop logic...
        item = slots[read_index.load() & (SIZE - 1)];
        read_index.store(read_index.load() + 1, std::memory_order_release);
        
        lock.unlock();
        not_full.notify_one();
        return true;
    }
};
```

Now the consumer loop becomes:

```cpp
void Lexer::worker(std::string thread) {
    while (running.load()) {
        ILP line;
        if (!lineQueue.pop_blocking(line)) {
            break;  // Queue shut down, exit
        }
        // ... process line ...
    }
}
```

And shutdown becomes:

```cpp
Error Lexer::Stop() {
    running_.store(false);
    lineQueue.request_shutdown();  // Wakes up all blocked threads
    
    if (worker1_.joinable()) worker1_.join();
    if (worker2_.joinable()) worker2_.join();
    
    return Error("");
}
```

Both approaches work. Poison pills are simpler. Shutdown-aware queues are cleaner at scale.

---

## The Control Plane: Startup Order

Your Kernal starts subsystems in registration order. This matters because of dependencies:

```
Store must start before Engine (Engine reads from Store)
Dir Reader must start before Lexer (Lexer reads from dir queue)
Lexer must start before Parser (Parser reads from parser queue)
```

Your current `InitAll()` and `StartAll()` do this correctly — they iterate `order_` which preserves registration order.

But there's a subtlety: **during startup, if one subsystem fails to start, you must stop all previously-started subsystems**.

Here's what your current `StartAll()` does:

```cpp
Error Kernal::StartAll() {
    for (const auto& name : order_) {
        Error error = subsystem->Start();
        if (HasError(error)) {
            return Error("[" + name + "] Start failed: " + error.GetMessage());
            // BUG: Previously started subsystems are left running!
        }
    }
    return Error("");
}
```

If the Parser fails to start, the Store, Dir Reader, and Lexer are left running with their threads active. When the Kernal destructor runs, those subsystems are destroyed without their threads being joined. **Crash.**

The correct pattern is **rollback on failure**:

```cpp
Error Kernal::StartAll() {
    std::vector<std::string> started;
    
    for (const auto& name : order_) {
        Subsystem* subsystem = subsystems_[name].get();
        Error error = subsystem->Start();
        
        if (HasError(error)) {
            // ROLLBACK: Stop everything we already started, in reverse order
            for (auto it = started.rbegin(); it != started.rend(); ++it) {
                Subsystem* started_sub = subsystems_[*it].get();
                started_sub->Stop();
            }
            return Error("[" + name + "] Start failed: " + error.GetMessage());
        }
        
        started.push_back(name);
    }
    
    return Error("");
}
```

This is the same concept as a **database transaction**: if anything fails, rollback everything.

---

## The Control Plane: Shutdown Order

Your `StopAll()` correctly iterates in reverse:

```cpp
for (auto it = order_.rbegin(); it != order_.rend(); ++it) {
    // Stop in reverse registration order
}
```

Why reverse? Because of dependency chains:

```
Startup order:   Store → Dir Reader → Lexer → Parser → Engine
Shutdown order:  Engine → Parser → Lexer → Dir Reader → Store
```

You stop consumers before producers. If you stopped the Dir Reader first (the producer), the Lexer (the consumer) would be stuck waiting for data that will never come. By stopping the Engine first (the furthest downstream consumer), you ensure nothing is trying to read data that's being shut down.

### Your Current `StopAll()` Has a Strength

```cpp
Error Kernal::StopAll() {
    std::string errors;
    
    for (auto it = order_.rbegin(); it != order_.rend(); ++it) {
        Error error = subsystem->Stop();
        if (HasError(error)) {
            errors += "[" + name + "] Stop failed: " + error.GetMessage() + "\n";
            // CONTINUES stopping the rest — this is correct!
        }
    }
}
```

Notice that on error, it **continues** stopping other subsystems instead of returning early. This is correct! During shutdown, you want to stop as much as possible, even if one subsystem fails. This is different from startup, where you abort early.

Think of it this way:
- **Startup**: If one thing fails, abort everything (you can't run a half-started system)
- **Shutdown**: If one thing fails, keep going (you want to clean up as much as possible)

---

## State Tracking: Preventing Double-Start and Zombie-Stop

Your current subsystem interface doesn't track state:

```cpp
class Subsystem {
    virtual Error Init() = 0;
    virtual Error Start() = 0;
    virtual Error Stop() = 0;
};
```

This allows bugs like:
- Calling `Start()` twice → creates duplicate threads
- Calling `Stop()` before `Start()` → joins threads that don't exist
- Calling `Init()` after `Start()` → undefined behavior

Here's how to add state tracking:

```cpp
class Subsystem {
public:
    enum class State {
        CREATED,
        INITIALIZED,
        STARTED,
        STOPPING,
        STOPPED
    };

    virtual ~Subsystem() {}
    virtual std::string Name() = 0;
    
    // These are called by the Kernal
    Error Init() {
        if (state_ != State::CREATED) {
            return Error(Name() + " cannot Init — current state is not CREATED");
        }
        Error err = OnInit();
        if (err.GetMessage().empty()) {
            state_ = State::INITIALIZED;
        }
        return err;
    }
    
    Error Start() {
        if (state_ != State::INITIALIZED) {
            return Error(Name() + " cannot Start — current state is not INITIALIZED");
        }
        Error err = OnStart();
        if (err.GetMessage().empty()) {
            state_ = State::STARTED;
        }
        return err;
    }
    
    Error Stop() {
        if (state_ != State::STARTED) {
            return Error(Name() + " cannot Stop — current state is not STARTED");
        }
        state_ = State::STOPPING;
        Error err = OnStop();
        state_ = State::STOPPED;
        return err;
    }
    
    State GetState() const { return state_; }

protected:
    // Subclasses implement these
    virtual Error OnInit() = 0;
    virtual Error OnStart() = 0;
    virtual Error OnStop() = 0;

private:
    State state_ = State::CREATED;
};
```

This is the **Template Method Pattern**. The base class controls the state machine. Subclasses implement `OnInit()`, `OnStart()`, `OnStop()` without worrying about state tracking.

Now if someone calls `Start()` twice:

```cpp
lexer->Start();  // ✅ State: INITIALIZED → STARTED
lexer->Start();  // ❌ Error: "Lexer cannot Start — current state is not INITIALIZED"
```

---

## Timeout-Based Shutdown

What if a thread refuses to stop? Maybe it's stuck in a system call, or it has a bug.

Your `Stop()` calls `join()`, which waits forever. In production systems, you add a timeout:

```cpp
Error Lexer::OnStop() {
    running_.store(false);
    lineQueue_.request_shutdown();
    
    // Try to join each thread with a timeout
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    
    // For join with timeout, you need a helper
    // since std::thread::join() has no timeout.
    // Common pattern: use a flag + timed wait
    
    if (worker1_.joinable()) {
        worker1_.join();  // In practice, consider using jthread or a wrapper
    }
    
    return Error("");
}
```

For now, simple `join()` is fine. But know that production systems add timeouts and escalation (e.g., "if the thread hasn't stopped in 5 seconds, log an error and force-terminate").

---

## Summary of Part 2

| Concept | Key Insight |
|---------|-------------|
| **Subsystems own their threads** | The Kernal doesn't create or join threads. Each subsystem manages its own. |
| **Init vs Start** | Init allocates resources. Start creates threads. Separate so Init can fail cheaply. |
| **Poison pills** | Send a special "stop" message into queues to unblock sleeping threads |
| **Rollback on startup failure** | If subsystem N fails to start, stop subsystems 1 through N-1 |
| **Continue on shutdown failure** | If subsystem N fails to stop, keep stopping the rest |
| **State machine** | Track CREATED → INITIALIZED → STARTED → STOPPING → STOPPED to prevent invalid transitions |
| **Shutdown-aware queues** | Alternative to poison pills: queues that can wake up all blocked threads |

---

## What's Next

- **Part 3**: Subsystem-owned workers — Deep dive into how each subsystem (Lexer, DirectoryReader, Parser) should manage its threads
- **Part 4**: The complete implementation — Putting everything together for your SonarSearch engine
