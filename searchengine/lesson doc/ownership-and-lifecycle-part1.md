# Ownership & Lifecycle — Part 1: The Mental Model

## Why This Matters

Coming from an API background, you're used to a world where:

- A request comes in, you process it, you return a response
- The framework handles startup and shutdown
- Memory is managed by a garbage collector or a framework
- "Stopping" means you just stop handling requests

In systems engineering, none of this is true.

**You** are the framework. **You** decide:
- What starts first
- What depends on what
- What happens when something fails
- What happens when it's time to shut down
- Who owns what memory, and when it gets freed

This document will teach you the foundational mental model for all of this.

---

## The Two Worlds

### API World (What You Know)

```
Request → Handler → Response → Done
```

Your code lives inside someone else's system. Express.js, Django, Spring — they all boot up, call your code, and shut down. You never think about it.

The lifecycle looks like:

```
Framework boots → Registers your routes → Opens port → Handles requests → Ctrl+C → Framework stops
```

You only wrote the middle part. The framework did everything else.

### Systems World (Where You Are Now)

```
You ARE the framework.
```

Your SonarSearch engine is a system. Right now in your `main.cpp`, you're doing what Express.js does internally:

```cpp
// You are the framework now
Kernal kernal;                          // Create the runtime
kernal.Register("Store", new Store());  // Register components
kernal.InitAll();                       // Initialize everything  
kernal.StartAll();                      // Start everything
// ... run ...
kernal.StopAll();                       // Shut everything down
```

This is a **control plane**. You're building one. Let's understand what that means.

---

## What Is a Control Plane?

A control plane is a component that manages the lifecycle of other components.

Think of it like this:

| Concept | API World | Systems World |
|---------|-----------|---------------|
| The thing that starts everything | Express.js / Django | Your `Kernal` |
| The things being managed | Route handlers | Subsystems (Lexer, Parser, Store, Engine) |
| How they're started | Framework calls them | You call `Init()` then `Start()` |
| How they're stopped | Process exits | You call `Stop()` in reverse order |
| Who owns them | The framework | Your `Kernal` (via `unique_ptr`) |

Your `Kernal` class IS a control plane. It:
1. **Registers** subsystems (stores them)
2. **Initializes** them in order
3. **Starts** them in order
4. **Stops** them in reverse order
5. **Destroys** them when the Kernal itself is destroyed

---

## Ownership in C++: Who Deletes This?

In JavaScript/Python, you never think about this. The garbage collector handles it.

In C++, every object that is created on the heap (with `new`) **must** be deleted. If it isn't deleted, you have a **memory leak**. If it's deleted twice, you have a **crash**.

So the fundamental question in C++ systems is:

> **Who owns this object? Who is responsible for deleting it?**

### Your Current Ownership Model

Let's trace what happens in your `main.cpp`:

```cpp
// Step 1: You create a Store on the heap
Error storeRegError = kernal.Register("Store", new Store());
```

Who owns this `Store`? Let's look at `Register()`:

```cpp
Error Kernal::Register(const std::string& name, Subsystem* subsystem) {
    // ...
    subsystems_[name] = std::unique_ptr<Subsystem>(subsystem);
    // The Kernal now owns it via unique_ptr
}
```

The moment you call `Register()`, the raw pointer is wrapped in a `std::unique_ptr`. The `Kernal` now owns it. When the `Kernal` is destroyed, its `subsystems_` map is destroyed, which destroys each `unique_ptr`, which deletes each `Subsystem`.

**This is correct ownership.** But there's a subtlety you might not realize.

### The Ownership Chain

```
main() creates Kernal (on the stack)
    └── Kernal owns subsystems_ map
         ├── "Store"     → unique_ptr<Subsystem> → Store object (heap)
         ├── "Lexer"     → unique_ptr<Subsystem> → Lexer object (heap)
         ├── "Dir Reader"→ unique_ptr<Subsystem> → DirectoryReader object (heap)
         ├── "Parser"    → unique_ptr<Subsystem> → Parser object (heap)
         └── "Engine"    → unique_ptr<Subsystem> → Engine object (heap)
```

When `main()` returns:
1. Local variable `kernal` goes out of scope
2. `Kernal` destructor runs
3. `subsystems_` map is destroyed
4. Each `unique_ptr<Subsystem>` is destroyed
5. Each `Subsystem` destructor runs (via virtual destructor)
6. All heap memory is freed

**This is RAII** — Resource Acquisition Is Initialization. The resource (heap memory) is tied to the lifetime of a stack object (`kernal`). When the stack object dies, the resource is cleaned up automatically.

---

## The Three Types of Ownership

### 1. Owning Pointer: `std::unique_ptr<T>`

```cpp
std::unique_ptr<Subsystem> owner = std::make_unique<Store>();
// 'owner' is THE owner. When 'owner' dies, the Store dies.
// Cannot be copied. Can only be moved.
```

**Rule**: One and only one `unique_ptr` owns the object at any time.

### 2. Borrowed Pointer: `T*` (raw pointer)

```cpp
Subsystem* borrowed = kernal.GetSubsystem("Store");
// 'borrowed' can USE the Store but does NOT own it.
// If the Store is deleted elsewhere, 'borrowed' becomes a dangling pointer.
```

**Rule**: Raw pointers are for "I want to use this, but someone else is responsible for its lifetime."

### 3. Shared Pointer: `std::shared_ptr<T>`

```cpp
std::shared_ptr<Store> shared = std::make_shared<Store>();
// Multiple shared_ptrs can point to the same object.
// The object is deleted when the LAST shared_ptr dies.
```

**Rule**: Use when multiple owners genuinely need to keep something alive. Usually a sign of unclear ownership.

### What You're Currently Using

In your codebase:

| Where | Type | Correct? |
|-------|------|----------|
| `Kernal::subsystems_` | `unique_ptr<Subsystem>` | ✅ Yes — Kernal owns subsystems |
| `Engine::store_` | `Store*` (raw pointer) | ✅ Yes — Engine borrows, Kernal owns |
| `Lexer` queues | `RingBuffer&` (reference) | ✅ Yes — Lexer borrows, main owns |
| `main()` thread creation | Commented out raw threads | ❌ Problem — who owns these threads? |

---

## The Problem You Have Right Now

Let's look at what's broken in your system. Your Lexer starts threads:

```cpp
// lexer.cpp — Start()
Error Lexer::Start(){
    worker1 = std::thread(&Lexer::worker, this, "1"); 
    worker2 = std::thread(&Lexer::worker, this, "2"); 
    return Error("");
};
```

But your Lexer's `Stop()` method doesn't join them:

```cpp
// lexer.cpp — Stop()
Error Lexer::Stop(){
    std::cout << "\n [" << Name() << "] Stopping..." << std::endl;
    // NOTHING ELSE. The threads are just abandoned.
    return Error("");
};
```

And in `main.cpp`, the threads for other subsystems are all commented out:

```cpp
// std::thread lexer_thread(&Lexer::Run, lx);
// std::thread dir_reader_thread(&DirectoryReader::Run, dirReader);
// std::thread parser_thread(&Parser::Run, parser);
```

**The core problem**: Nobody owns the threads. Nobody signals them to stop. Nobody waits for them to finish.

When the Kernal calls `StopAll()` and then destroys the subsystems:
1. The Lexer's `unique_ptr` is destroyed
2. The Lexer destructor runs
3. But `worker1` and `worker2` are still running!
4. The `std::thread` destructor calls `std::terminate()` because a joinable thread was destroyed without being joined
5. **Your program crashes** (or the OS forcefully kills it)

---

## The Two Golden Rules of Thread Ownership

### Rule 1: Whoever Creates a Thread Must Join or Detach It

A `std::thread` in C++ is like a raw pointer to a running task. If you created it, you must:
- **Join it** (`thread.join()`) — wait for it to finish
- **Detach it** (`thread.detach()`) — let it run independently (almost never what you want)

If you do neither, and the `std::thread` object is destroyed while the thread is still running, your program calls `std::terminate()` and crashes.

### Rule 2: Before Joining, You Must Tell the Thread to Stop

Calling `join()` waits forever. If the thread is in an infinite loop, `join()` will hang forever. You need a way to signal the thread: "Hey, it's time to stop."

Common signaling mechanisms:
- **Atomic flag**: `std::atomic<bool> running{true};` → set to `false` to signal stop
- **Poison pill**: Push a special "empty" message into a queue
- **Condition variable**: Wake up a sleeping thread
- **Combination**: All of the above (which is what you'll need)

---

## What "Graceful Shutdown" Actually Means

"Graceful" means:

1. **Signal**: Tell every running thread "it's time to stop"
2. **Drain**: Let threads finish processing their current work item
3. **Wait**: Block until every thread has actually exited
4. **Cleanup**: Release resources (close files, flush buffers, free memory)
5. **Report**: Log what happened during shutdown

"Ungraceful" (crash) means:

1. The OS kills your process
2. File handles leak
3. Partial writes corrupt data
4. Threads are killed mid-operation
5. Users lose data

The difference between junior and senior systems engineers is understanding that **shutdown is harder than startup**.

---

## The Lifecycle State Machine

Every subsystem should go through a strict sequence of states:

```
CREATED → INITIALIZED → STARTED → STOPPING → STOPPED
```

```
┌──────────┐    Init()    ┌─────────────┐    Start()    ┌──────────┐
│ CREATED  │────────────→│ INITIALIZED │────────────→ │ STARTED  │
└──────────┘              └─────────────┘              └──────────┘
                                                            │
                                                       Stop()
                                                            │
                                                            ▼
                                                      ┌──────────┐
                                                      │ STOPPING │
                                                      └──────────┘
                                                            │
                                                    threads joined
                                                            │
                                                            ▼
                                                      ┌──────────┐
                                                      │ STOPPED  │
                                                      └──────────┘
```

Your current `Subsystem` interface doesn't track state. It just has methods. This means:
- You can call `Start()` before `Init()`
- You can call `Stop()` on something that was never started
- You can call `Start()` twice

All of these are bugs waiting to happen. Part 2 will show you how to fix this.

---

## Summary of Part 1

| Concept | What You Need to Know |
|---------|----------------------|
| **Control Plane** | Your `Kernal` — it orchestrates lifecycle of all subsystems |
| **Ownership** | `unique_ptr` = owner, raw pointer = borrower, never delete what you don't own |
| **RAII** | Tie resource lifetime to object lifetime. Stack objects auto-cleanup |
| **Thread Ownership** | Whoever creates a thread must join it. Period. |
| **Graceful Shutdown** | Signal → Drain → Wait → Cleanup → Report |
| **State Machine** | Every subsystem should track: CREATED → INIT → STARTED → STOPPING → STOPPED |

---

## What's Next

- **Part 2**: The Control Plane pattern — How the Kernal should orchestrate subsystem and thread lifecycle
- **Part 3**: Subsystem-owned workers — How each subsystem should manage its own threads
- **Part 4**: Putting it all together — The complete implementation for your SonarSearch engine
