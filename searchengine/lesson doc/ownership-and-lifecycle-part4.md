# Ownership & Lifecycle — Part 4: Complete Implementation Guide

## Where We Left Off

Part 1: Mental model — ownership, RAII, what a control plane is.  
Part 2: Control plane pattern — how the Kernal orchestrates lifecycle.  
Part 3: Worker ownership — how each subsystem manages its threads.  

Now we bring it all together. This document shows the complete corrected implementation of your SonarSearch engine with proper lifecycle management. Every code block is annotated with explanations.

---

## The Big Picture: Before vs After

### Before (Current State)

```
main.cpp:
  - Creates Kernal
  - Registers subsystems
  - Calls InitAll() and StartAll()
  - Manually creates threads for subsystems (commented out)
  - Manually joins threads (commented out)
  - Calls StopAll()

Problems:
  - Threads created outside subsystem ownership
  - Lexer::Stop() doesn't join its workers
  - No poison pills on shutdown
  - No state tracking
  - No startup rollback
  - main.cpp does too much
```

### After (What We're Building)

```
main.cpp:
  - Creates Kernal
  - Registers subsystems
  - Calls InitAll() and StartAll()
  - Waits for shutdown signal (Ctrl+C or Enter)
  - Calls StopAll()
  - That's it. 6 lines of logic.

Each Subsystem:
  - Owns its threads
  - Creates them in Start()
  - Joins them in Stop()
  - Sends poison pills to unblock queues
  - Tracks its own state
```

---

## Step 1: Enhanced Subsystem Base Class

This is the foundation. Every subsystem inherits from this.

```cpp
// internal/kernal/core/headerfiles/subsystem.hpp
#pragma once
#include <string>
#include <atomic>
#include <iostream>
#include "internal/kernal/core/headerfiles/error.hpp"

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

    // Template Method Pattern: base class controls state transitions,
    // subclasses implement the actual logic in On*() methods.
    
    Error Init() {
        if (state_ != State::CREATED) {
            return Error("[" + Name() + "] Cannot Init — not in CREATED state");
        }
        std::cout << "\n [" << Name() << "] Initializing..." << std::endl;
        Error err = OnInit();
        if (err.GetMessage().empty()) {
            state_ = State::INITIALIZED;
        }
        return err;
    }

    Error Start() {
        if (state_ != State::INITIALIZED) {
            return Error("[" + Name() + "] Cannot Start — not in INITIALIZED state");
        }
        std::cout << "\n [" << Name() << "] Starting..." << std::endl;
        Error err = OnStart();
        if (err.GetMessage().empty()) {
            state_ = State::STARTED;
        }
        return err;
    }

    Error Stop() {
        if (state_ != State::STARTED) {
            // During shutdown, it's OK if something isn't started.
            // Just log and return success.
            std::cout << "\n [" << Name() << "] Not started, skipping stop" << std::endl;
            return Error("");
        }
        std::cout << "\n [" << Name() << "] Stopping..." << std::endl;
        state_ = State::STOPPING;
        Error err = OnStop();
        state_ = State::STOPPED;
        std::cout << "\n [" << Name() << "] Stopped" << std::endl;
        return err;
    }

    State GetState() const { return state_; }

    bool IsRunning() const { return state_ == State::STARTED; }

protected:
    // Subclasses implement these
    virtual Error OnInit() = 0;
    virtual Error OnStart() = 0;
    virtual Error OnStop() = 0;

    // Shared running flag — subclass threads check this
    std::atomic<bool> running_{false};

private:
    State state_ = State::CREATED;
};
```

### What Changed and Why

1. **State enum**: Prevents invalid transitions (double-start, stop-before-start).
2. **Template Method**: `Init()/Start()/Stop()` are now non-virtual. They manage state, then delegate to `OnInit()/OnStart()/OnStop()`.
3. **`running_` flag**: Moved to base class since every subsystem with threads needs it.
4. **Logging**: Centralized in the base class instead of each subclass.

---

## Step 2: Enhanced Kernal (Control Plane)

```cpp
// internal/kernal/kernal.hpp
#pragma once
#include <map>
#include <string>
#include <vector>
#include <memory>
#include "internal/kernal/core/headerfiles/subsystem.hpp"
#include "internal/kernal/core/headerfiles/error.hpp"

class Kernal {
public:
    Kernal() = default;
    
    // Non-copyable, non-movable — there is exactly one Kernal
    Kernal(const Kernal&) = delete;
    Kernal& operator=(const Kernal&) = delete;
    Kernal(Kernal&&) = delete;
    Kernal& operator=(Kernal&&) = delete;
    
    ~Kernal();

    std::string GetName() const;
    Error Register(const std::string& name, Subsystem* subsystem);
    Error InitAll();
    Error StartAll();
    Error StopAll();
    Subsystem* GetSubsystem(const std::string& name) const;

private:
    std::map<std::string, std::unique_ptr<Subsystem>> subsystems_;
    std::vector<std::string> order_;
    bool HasError(const Error& error) const;
};
```

```cpp
// internal/kernal/kernal.cpp
#include "Kernal.hpp"
#include <iostream>

Kernal::~Kernal() {
    // Safety net: if someone forgot to call StopAll(), do it now.
    // This prevents threads from being destroyed while still running.
    for (auto it = order_.rbegin(); it != order_.rend(); ++it) {
        Subsystem* sub = subsystems_[*it].get();
        if (sub->GetState() == Subsystem::State::STARTED) {
            std::cerr << "\n [" << GetName() << "] WARNING: " << *it 
                      << " was not stopped before Kernal destruction. "
                      << "Stopping now." << std::endl;
            sub->Stop();
        }
    }
    // unique_ptrs will delete all subsystems automatically
}

std::string Kernal::GetName() const {
    return "Kernal";
}

bool Kernal::HasError(const Error& error) const {
    return !error.GetMessage().empty();
}

Error Kernal::Register(const std::string& name, Subsystem* subsystem) {
    if (subsystems_.find(name) != subsystems_.end()) {
        delete subsystem;
        return Error("[" + name + "] Subsystem already registered");
    }

    subsystems_[name] = std::unique_ptr<Subsystem>(subsystem);
    order_.push_back(name);
    std::cout << "\n [" << GetName() << "] [" << name << "] Registered" << std::endl;
    return Error("");
}

Error Kernal::InitAll() {
    std::cout << "\n [" << GetName() << "] Initializing all subsystems..." << std::endl;

    for (const auto& name : order_) {
        Error error = subsystems_[name]->Init();
        if (HasError(error)) {
            return error;
        }
    }

    std::cout << "\n [" << GetName() << "] All subsystems initialized" << std::endl;
    return Error("");
}

Error Kernal::StartAll() {
    std::cout << "\n [" << GetName() << "] Starting all subsystems..." << std::endl;

    std::vector<std::string> started;

    for (const auto& name : order_) {
        Error error = subsystems_[name]->Start();

        if (HasError(error)) {
            std::cerr << "\n [" << GetName() << "] [" << name 
                      << "] Start failed, rolling back..." << std::endl;
            
            // ROLLBACK: Stop everything we already started, in reverse
            for (auto it = started.rbegin(); it != started.rend(); ++it) {
                std::cerr << "\n [" << GetName() << "] Rolling back [" << *it << "]" << std::endl;
                subsystems_[*it]->Stop();
            }
            
            return error;
        }

        started.push_back(name);
    }

    std::cout << "\n [" << GetName() << "] All subsystems started" << std::endl;
    return Error("");
}

Error Kernal::StopAll() {
    std::cout << "\n [" << GetName() << "] Stopping all subsystems..." << std::endl;

    std::string errors;

    // Reverse order: stop downstream consumers first
    for (auto it = order_.rbegin(); it != order_.rend(); ++it) {
        const std::string& name = *it;
        Error error = subsystems_[name]->Stop();

        if (HasError(error)) {
            errors += "[" + name + "] " + error.GetMessage() + "\n";
            // CONTINUE stopping the rest — don't abort on error
        }
    }

    if (!errors.empty()) {
        return Error("Shutdown errors:\n" + errors);
    }

    std::cout << "\n [" << GetName() << "] All subsystems stopped" << std::endl;
    return Error("");
}

Subsystem* Kernal::GetSubsystem(const std::string& name) const {
    auto it = subsystems_.find(name);
    if (it != subsystems_.end()) {
        return it->second.get();
    }
    return nullptr;
}
```

### What Changed and Why

1. **Destructor safety net**: If `StopAll()` wasn't called, the destructor calls `Stop()` on any still-running subsystems. This prevents `std::terminate()` from threads being destroyed while running.
2. **Startup rollback**: If subsystem N fails to start, subsystems 1 through N-1 are stopped in reverse order.
3. **Shutdown continues on error**: During `StopAll()`, errors are accumulated but don't stop the shutdown process.

---

## Step 3: Store — The Passive Subsystem

```cpp
// internal/store/store.hpp
#pragma once
#include "internal/kernal/core/headerfiles/subsystem.hpp"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

class Store : public Subsystem {
public:
    Store() = default;
    std::string Name() override;
    void AddDataFile(const std::string& filePath);
    std::vector<std::string> GetDataFiles() const;
    void AddSearchIndex(std::string key, const std::string& doc);
    const std::unordered_map<std::string, std::vector<std::string>>& GetSearchIndex() const;

protected:
    Error OnInit() override;
    Error OnStart() override;
    Error OnStop() override;

private:
    std::vector<std::string> dataFiles_;
    std::unordered_map<std::string, std::vector<std::string>> searchIndex_;
};
```

```cpp
// internal/store/store.cpp
#include "store.hpp"
#include <iostream>

std::string Store::Name() { return "Store"; }

Error Store::OnInit() {
    // No resources to allocate for a passive store
    return Error("");
}

Error Store::OnStart() {
    // No threads to create — Store is passive
    return Error("");
}

Error Store::OnStop() {
    // No threads to join
    // Could flush to disk here if we supported persistence
    return Error("");
}

void Store::AddDataFile(const std::string& filePath) {
    dataFiles_.push_back(filePath);
}

std::vector<std::string> Store::GetDataFiles() const {
    return dataFiles_;
}

void Store::AddSearchIndex(std::string key, const std::string& doc) {
    searchIndex_[key].push_back(doc);
}

const std::unordered_map<std::string, std::vector<std::string>>& Store::GetSearchIndex() const {
    return searchIndex_;
}
```

### What Changed

- `OnInit()/OnStart()/OnStop()` replace `Init()/Start()/Stop()` (Template Method Pattern).
- No behavior change — Store is already correct since it has no threads.

---

## Step 4: DirectoryReader — Single Thread Subsystem

```cpp
// internal/directoryreader/directoryreader.hpp
#pragma once
#include "internal/kernal/core/headerfiles/subsystem.hpp"
#include "internal/kernal/core/datastructures/ringbuffer.hpp"
#include <string>
#include <thread>

class DirectoryReader : public Subsystem {
public:
    DirectoryReader(RingBuffer<std::string, 1024>& dirQueue) 
        : dirQueue_(dirQueue) {}
    
    std::string Name() override;

protected:
    Error OnInit() override;
    Error OnStart() override;
    Error OnStop() override;

private:
    RingBuffer<std::string, 1024>& dirQueue_;  // borrowed reference
    std::thread run_thread_;                    // owned thread
    
    void Run();  // thread entry point — now private
};
```

```cpp
// internal/directoryreader/directoryreader.cpp
#include "directoryreader.hpp"
#include <iostream>
#include <filesystem>

std::string DirectoryReader::Name() { return "Dir Reader"; }

Error DirectoryReader::OnInit() {
    // Could validate that the "data" directory exists here
    if (!std::filesystem::exists("data") || !std::filesystem::is_directory("data")) {
        return Error("Data directory not found");
    }
    return Error("");
}

Error DirectoryReader::OnStart() {
    running_.store(true, std::memory_order_release);
    
    // DirectoryReader owns this thread. It creates it here, joins it in OnStop().
    run_thread_ = std::thread(&DirectoryReader::Run, this);
    
    return Error("");
}

Error DirectoryReader::OnStop() {
    // 1. Signal the thread to stop
    running_.store(false, std::memory_order_release);
    
    // 2. The Run() method might be blocked on push_blocking if queue is full.
    //    With the current implementation, this is unlikely since the Lexer
    //    should be draining the queue. But in a production system, you'd
    //    want shutdown-aware queues here.
    
    // 3. Wait for the thread to finish
    if (run_thread_.joinable()) {
        run_thread_.join();
    }
    
    return Error("");
}

void DirectoryReader::Run() {
    std::cout << "\n [" << Name() << "] Scanning directory..." << std::endl;
    
    std::string dataPath = "data";
    
    for (const auto& entry : std::filesystem::directory_iterator(dataPath)) {
        // Check shutdown flag between iterations
        if (!running_.load(std::memory_order_acquire)) {
            std::cout << "\n [" << Name() << "] Shutdown during scan, aborting" << std::endl;
            break;
        }
        
        if (entry.is_regular_file()) {
            dirQueue_.push_blocking(entry.path().string());
        }
    }
    
    // Send poison pill so downstream (Lexer::Run) knows we're done
    dirQueue_.push_blocking(std::string(""));
    
    std::cout << "\n [" << Name() << "] Scan complete" << std::endl;
}
```

### What Changed and Why

1. **`Run()` is now private**: Only `Start()` calls it via a thread. Nobody outside can call it directly.
2. **Thread created in `OnStart()`**: DirectoryReader owns the thread.
3. **Thread joined in `OnStop()`**: Complete ownership lifecycle.
4. **Shutdown check in loop**: If the Kernal calls `StopAll()` during a directory scan, the thread exits early instead of scanning the entire directory.
5. **Poison pill preserved**: The poison pill at the end of `Run()` tells the Lexer that directory scanning is complete. This is a **data flow** signal, not a **lifecycle** signal.

---

## Step 5: Lexer — Multi-Thread Subsystem

This is the most complex subsystem. Pay close attention to the shutdown sequence.

```cpp
// internal/lexer/lexer.hpp
#pragma once
#include "internal/kernal/core/headerfiles/subsystem.hpp"
#include "internal/kernal/core/datastructures/ringbuffer.hpp"
#include "ilp.hpp"
#include <thread>
#include <vector>
#include <string>
#include <unordered_set>

class Lexer : public Subsystem {
public:
    Lexer(
        RingBuffer<std::string, 1024>& dirQueue,
        RingBuffer<ILP, 1024>& lineQueue, 
        RingBuffer<std::string, 1024>& parserQueue) 
        : dirQueue_(dirQueue), lineQueue_(lineQueue), parserQueue_(parserQueue) {}

    std::string Name() override;

protected:
    Error OnInit() override;
    Error OnStart() override;
    Error OnStop() override;

private:
    // Borrowed references (owned by main)
    RingBuffer<std::string, 1024>& dirQueue_;
    RingBuffer<ILP, 1024>& lineQueue_;
    RingBuffer<std::string, 1024>& parserQueue_;
    
    // Owned threads
    std::thread run_thread_;    // coordinator: reads files, feeds lineQueue
    std::thread worker1_;       // worker: processes lines
    std::thread worker2_;       // worker: processes lines
    
    // Thread entry points
    void Run();
    void Worker(std::string id);
    
    // Helpers
    std::vector<std::string> splitLine(std::string& line);
    bool isStopWord(std::string& word, std::unordered_set<std::string>& stopWord);
};
```

```cpp
// internal/lexer/lexer.cpp
#include "lexer.hpp"
#include <iostream>
#include <algorithm>
#include <cctype>
#include "internal/kernal/core/utils/stopwords.hpp"

std::string Lexer::Name() { return "Lexer"; }

Error Lexer::OnInit() {
    // Could pre-load stop word lists here
    return Error("");
}

Error Lexer::OnStart() {
    running_.store(true, std::memory_order_release);
    
    try {
        // Start workers FIRST — they need to be ready to consume
        // before the coordinator starts producing.
        worker1_ = std::thread(&Lexer::Worker, this, "1");
        worker2_ = std::thread(&Lexer::Worker, this, "2");
        
        // Then start the coordinator
        run_thread_ = std::thread(&Lexer::Run, this);
    } catch (const std::system_error& e) {
        // If thread creation fails, clean up any threads that were created
        running_.store(false, std::memory_order_release);
        
        // Send poison pills to any workers that started
        lineQueue_.push_blocking(ILP{"", ""});
        lineQueue_.push_blocking(ILP{"", ""});
        
        if (worker1_.joinable()) worker1_.join();
        if (worker2_.joinable()) worker2_.join();
        if (run_thread_.joinable()) run_thread_.join();
        
        return Error("Thread creation failed: " + std::string(e.what()));
    }
    
    return Error("");
}

Error Lexer::OnStop() {
    // === STEP 1: Signal all threads to stop ===
    running_.store(false, std::memory_order_release);
    
    // === STEP 2: Unblock the Run thread ===
    // Run() is blocked on dirQueue_.pop_blocking().
    // Send a poison pill so it wakes up and sees running_ is false.
    dirQueue_.push_blocking(std::string(""));
    
    // === STEP 3: Wait for the Run thread to exit ===
    // We join the coordinator FIRST because:
    // - The coordinator might still be pushing items to lineQueue
    // - If we kill workers first, the coordinator blocks on push_blocking
    // - By joining the coordinator first, we know no more items will be pushed
    if (run_thread_.joinable()) {
        run_thread_.join();
    }
    
    // === STEP 4: Unblock worker threads ===
    // Workers are blocked on lineQueue_.pop_blocking().
    // Send one poison pill per worker.
    lineQueue_.push_blocking(ILP{"", ""});  // For worker 1
    lineQueue_.push_blocking(ILP{"", ""});  // For worker 2
    
    // === STEP 5: Wait for workers to exit ===
    if (worker1_.joinable()) {
        worker1_.join();
    }
    if (worker2_.joinable()) {
        worker2_.join();
    }
    
    // === STEP 6: Signal downstream (Parser) that we're done ===
    parserQueue_.push_blocking(std::string(""));
    
    std::cout << "\n [" << Name() << "] All threads joined" << std::endl;
    return Error("");
}

void Lexer::Run() {
    std::cout << "\n [" << Name() << "] Coordinator started" << std::endl;
    
    int filesProcessed = 0;
    
    while (running_.load(std::memory_order_acquire)) {
        std::string file;
        dirQueue_.pop_blocking(file);
        
        // Check for poison pill (from DirectoryReader or from our own Stop)
        if (file.empty()) {
            std::cout << "\n [" << Name() << "] Coordinator received end signal" << std::endl;
            break;
        }
        
        // Open and read the file
        FILE* fp = fopen(file.c_str(), "r");
        if (!fp) {
            std::cerr << "\n [" << Name() << "] Failed to open: " << file << std::endl;
            continue;
        }
        
        char line[456];
        while (fgets(line, sizeof(line), fp) != nullptr) {
            // Check shutdown between lines
            if (!running_.load(std::memory_order_acquire)) {
                break;
            }
            
            size_t len = strlen(line);
            if (len > 0 && line[len - 1] == '\n') {
                line[len - 1] = '\0';
            }
            
            lineQueue_.push_blocking({file, std::string(line)});
        }
        
        if (ferror(fp)) {
            std::cerr << "\n [" << Name() << "] Read error on: " << file << std::endl;
        }
        
        fclose(fp);
        filesProcessed++;
    }
    
    std::cout << "\n [" << Name() << "] Coordinator done. Files: " << filesProcessed << std::endl;
}

void Lexer::Worker(std::string id) {
    std::cout << "\n [" << Name() << "] Worker " << id << " started" << std::endl;
    
    int tokensProcessed = 0;
    
    while (running_.load(std::memory_order_acquire)) {
        ILP line;
        lineQueue_.pop_blocking(line);
        
        // Check for poison pill
        if (line.filepath.empty() && line.line.empty()) {
            std::cout << "\n [" << Name() << "] Worker " << id 
                      << " received shutdown signal" << std::endl;
            break;
        }
        
        // Tokenize and process the line
        auto tokens = splitLine(line.line);
        for (auto& token : tokens) {
            // Remove punctuation and whitespace
            token.erase(
                std::remove_if(token.begin(), token.end(),
                    [](unsigned char c) {
                        return std::isspace(c) || std::ispunct(c);
                    }
                ),
                token.end()
            );
            
            // Skip empty tokens and stop words
            if (!token.empty() && StopWords::isStopWord(token)) {
                parserQueue_.push_blocking(token);
                tokensProcessed++;
            }
        }
    }
    
    std::cout << "\n [" << Name() << "] Worker " << id << " done. Tokens: " 
              << tokensProcessed << std::endl;
}

std::vector<std::string> Lexer::splitLine(std::string& line) {
    std::vector<std::string> result;
    std::size_t start = 0;
    std::size_t end = line.find(' ');

    while (end != std::string::npos) {
        result.push_back(line.substr(start, end - start));
        start = end + 1;
        end = line.find(' ', start);
    }
    result.push_back(line.substr(start));
    return result;
}
```

### The Critical Shutdown Sequence Explained

The Lexer shutdown is a 6-step dance. Let's trace through it:

```
Time    Main Thread              Run Thread              Worker 1              Worker 2
────    ───────────              ──────────              ────────              ────────
T0      OnStop() called
T1      running_ = false
T2      push "" to dirQueue
T3      (waiting for join)       wakes up, sees ""
T4                               checks running: false
T5                               exits Run()
T6      run_thread_ joined ✓
T7      push ILP{"",""} to lineQueue                    wakes up, sees {"",""}
T8      push ILP{"",""} to lineQueue                                          wakes up, sees {"",""}
T9      (waiting for join)                              exits Worker()        exits Worker()
T10     worker1_ joined ✓
T11     worker2_ joined ✓
T12     push "" to parserQueue
T13     OnStop() returns ✓
```

Every step has a purpose. Skip any step and you get a hang or a crash.

---

## Step 6: Parser

```cpp
// internal/parser/parser.hpp
#pragma once
#include "internal/kernal/core/headerfiles/subsystem.hpp"
#include "internal/kernal/core/datastructures/ringbuffer.hpp"
#include <thread>

class Parser : public Subsystem {
public:
    Parser(RingBuffer<std::string, 1024>& parserQueue) 
        : parserQueue_(parserQueue) {}
    
    std::string Name() override;

protected:
    Error OnInit() override;
    Error OnStart() override;
    Error OnStop() override;

private:
    RingBuffer<std::string, 1024>& parserQueue_;
    std::thread run_thread_;
    
    void Run();
};
```

```cpp
// internal/parser/parser.cpp
#include "parser.hpp"
#include <iostream>

std::string Parser::Name() { return "Parser"; }

Error Parser::OnInit() {
    return Error("");
}

Error Parser::OnStart() {
    running_.store(true, std::memory_order_release);
    run_thread_ = std::thread(&Parser::Run, this);
    return Error("");
}

Error Parser::OnStop() {
    running_.store(false, std::memory_order_release);
    
    // Unblock the Run thread if it's waiting on pop_blocking
    parserQueue_.push_blocking(std::string(""));
    
    if (run_thread_.joinable()) {
        run_thread_.join();
    }
    
    return Error("");
}

void Parser::Run() {
    std::cout << "\n [" << Name() << "] Processing started" << std::endl;
    
    int tokensProcessed = 0;
    
    while (running_.load(std::memory_order_acquire)) {
        std::string token;
        parserQueue_.pop_blocking(token);
        
        // Check for poison pill
        if (token.empty()) {
            std::cout << "\n [" << Name() << "] Received end signal" << std::endl;
            break;
        }
        
        // Process the token
        std::cout << "\n [" << Name() << "] Token: " << token << std::endl;
        tokensProcessed++;
    }
    
    std::cout << "\n [" << Name() << "] Done. Tokens: " << tokensProcessed << std::endl;
}
```

---

## Step 7: Engine

```cpp
// internal/engine/engine.hpp
#pragma once
#include "internal/kernal/core/headerfiles/subsystem.hpp"
#include "internal/store/store.hpp"
#include <thread>
#include <string>

class Engine : public Subsystem {
public:
    Engine(Store* store) : store_(store) {}
    std::string Name() override;

protected:
    Error OnInit() override;
    Error OnStart() override;
    Error OnStop() override;

private:
    Store* store_;               // borrowed pointer — Kernal owns Store
    std::thread run_thread_;     // owned thread
    
    void Run();
    std::string Prompt();
    void Search(std::string& query);
};
```

```cpp
// internal/engine/engine.cpp
#include "engine.hpp"
#include <iostream>
#include <algorithm>

std::string Engine::Name() { return "Search Engine"; }

Error Engine::OnInit() {
    if (!store_) {
        return Error("Store pointer is null");
    }
    return Error("");
}

Error Engine::OnStart() {
    running_.store(true, std::memory_order_release);
    run_thread_ = std::thread(&Engine::Run, this);
    return Error("");
}

Error Engine::OnStop() {
    running_.store(false, std::memory_order_release);
    
    // Problem: if Run() is blocked on std::cin, we can't unblock it easily.
    // Options:
    //   1. Don't block on cin in the Run thread (use non-blocking IO)
    //   2. Accept that the user must press Enter to unblock
    //   3. Use platform-specific tricks (e.g., close stdin)
    //
    // For now, we accept option 2 — the main thread waits for Enter
    // before calling StopAll(), so the Engine thread should already be
    // past the cin.get() call.
    
    if (run_thread_.joinable()) {
        run_thread_.join();
    }
    
    return Error("");
}

void Engine::Run() {
    std::cout << "\n [" << Name() << "] Ready for queries" << std::endl;
    
    while (running_.load(std::memory_order_acquire)) {
        std::string query = Prompt();
        
        if (query.empty() || !running_.load()) {
            break;
        }
        
        Search(query);
    }
    
    std::cout << "\n [" << Name() << "] Query loop exited" << std::endl;
}

std::string Engine::Prompt() {
    std::string prompt;
    std::cout << "\n [" << Name() << "] Search: ";
    std::getline(std::cin, prompt);
    return prompt;
}

void Engine::Search(std::string& query) {
    std::transform(query.begin(), query.end(), query.begin(),
        [](unsigned char c) { return std::tolower(c); });
    
    const auto& index = store_->GetSearchIndex();
    auto it = index.find(query);
    
    std::cout << "\n Search Results: " << std::endl;
    
    if (it == index.end()) {
        std::cout << " No results found." << std::endl;
        return;
    }
    
    for (const auto& doc : it->second) {
        std::cout << " " << doc << std::endl;
    }
}
```

---

## Step 8: The Simplified main.cpp

```cpp
// cmd/main.cpp
#include <iostream>
#include "internal/kernal/kernal.hpp"
#include "internal/engine/engine.hpp"
#include "internal/parser/parser.hpp"
#include "internal/directoryreader/directoryreader.hpp"
#include "internal/store/store.hpp"
#include "internal/lexer/lexer.hpp"
#include "internal/kernal/core/datastructures/ringbuffer.hpp"

int main() {
    std::cout << "SonarSearch Starting..." << std::endl;

    // ===== 1. Create the control plane =====
    Kernal kernal;

    // ===== 2. Create shared queues (owned by main) =====
    RingBuffer<std::string, 1024> dirQueue;
    RingBuffer<ILP, 1024> lineQueue;
    RingBuffer<std::string, 1024> parserQueue;

    // ===== 3. Register subsystems (Kernal takes ownership) =====
    // Order matters! Dependencies must be registered first.
    
    // Store: no dependencies, passive data store
    kernal.Register("Store", new Store());
    
    // DirectoryReader: produces file paths → dirQueue
    kernal.Register("Dir Reader", new DirectoryReader(dirQueue));
    
    // Lexer: consumes dirQueue, produces to lineQueue and parserQueue
    kernal.Register("Lexer", new Lexer(dirQueue, lineQueue, parserQueue));
    
    // Parser: consumes parserQueue
    kernal.Register("Parser", new Parser(parserQueue));
    
    // Engine: reads from Store
    Store* store = dynamic_cast<Store*>(kernal.GetSubsystem("Store"));
    kernal.Register("Search Engine", new Engine(store));

    // ===== 4. Initialize all (validate, allocate, pre-flight check) =====
    Error initError = kernal.InitAll();
    if (!initError.GetMessage().empty()) {
        std::cerr << "Init failed: " << initError.GetMessage() << std::endl;
        return 1;
    }

    // ===== 5. Start all (create threads, begin processing) =====
    Error startError = kernal.StartAll();
    if (!startError.GetMessage().empty()) {
        std::cerr << "Start failed: " << startError.GetMessage() << std::endl;
        return 1;
    }

    // ===== 6. Wait for shutdown signal =====
    std::cout << "\nSonarSearch Running. Press Enter to exit..." << std::endl;
    std::cin.get();

    // ===== 7. Graceful shutdown =====
    Error stopError = kernal.StopAll();
    if (!stopError.GetMessage().empty()) {
        std::cerr << "Shutdown errors:\n" << stopError.GetMessage() << std::endl;
        return 1;
    }

    std::cout << "SonarSearch stopped cleanly." << std::endl;
    return 0;
}
```

### What Changed in main.cpp

**Before**: main.cpp manually created threads, cast subsystem pointers, and managed thread joining.  
**After**: main.cpp just registers subsystems and lets the Kernal handle everything.

The thread management is completely invisible to `main()`. Each subsystem handles its own threads internally. main() just says "start" and "stop."

---

## The Complete Shutdown Trace

Here's what happens when the user presses Enter:

```
User presses Enter
    │
    ▼
main() calls kernal.StopAll()
    │
    ├── Engine.Stop()
    │   ├── running_ = false
    │   ├── Engine::Run() sees running_ is false, exits loop
    │   ├── run_thread_.join() completes
    │   └── ✓ Engine stopped
    │
    ├── Parser.Stop()
    │   ├── running_ = false
    │   ├── push "" to parserQueue (unblocks Pop)
    │   ├── Parser::Run() receives "", exits loop
    │   ├── run_thread_.join() completes
    │   └── ✓ Parser stopped
    │
    ├── Lexer.Stop()
    │   ├── running_ = false
    │   ├── push "" to dirQueue (unblocks Run coordinator)
    │   ├── run_thread_.join() completes
    │   ├── push ILP{"",""} to lineQueue (unblocks worker1)
    │   ├── push ILP{"",""} to lineQueue (unblocks worker2)
    │   ├── worker1_.join() completes
    │   ├── worker2_.join() completes
    │   ├── push "" to parserQueue (for Parser, though it's already stopped)
    │   └── ✓ Lexer stopped
    │
    ├── DirReader.Stop()
    │   ├── running_ = false
    │   ├── DirReader::Run() already exited naturally or exits now
    │   ├── run_thread_.join() completes
    │   └── ✓ Dir Reader stopped
    │
    └── Store.Stop()
        └── ✓ Store stopped (nothing to do)

Kernal destructor runs
    └── All unique_ptrs destroyed → all Subsystem objects freed → ✓ Clean exit
```

Every thread is accounted for. Every thread is joined. Every resource is freed.

---

## Quick Reference: The Rules

### Rule 1: Subsystems Own Their Threads
```
✅ Threads created in OnStart(), joined in OnStop()
❌ Threads created in main.cpp or anywhere outside the subsystem
```

### Rule 2: One Poison Pill Per Consumer Thread
```
✅ 2 workers on lineQueue → push 2 poison pills
❌ 1 poison pill for 2 workers → one worker hangs forever
```

### Rule 3: Signal Before Join
```
✅ running_ = false → push poison pills → join
❌ join() without signaling → hangs forever
```

### Rule 4: Start Consumers Before Producers
```
✅ Start workers, then start coordinator
❌ Start coordinator first → queue fills up before workers start
```

### Rule 5: Stop Downstream Before Upstream
```
✅ Stop Engine → Parser → Lexer → DirReader → Store
❌ Stop DirReader first → Lexer blocks on empty dirQueue
```

### Rule 6: Continue Shutdown On Error
```
✅ If Parser.Stop() fails, keep stopping Lexer, DirReader, Store
❌ If Parser.Stop() fails, abort shutdown → resources leak
```

### Rule 7: Rollback Startup On Error
```
✅ If Parser.Start() fails, stop Lexer, DirReader, Store
❌ If Parser.Start() fails, just return error → running threads leak
```

---

## What You've Learned

This 4-part series taught you:

1. **Ownership** — Who is responsible for deleting each object and joining each thread
2. **RAII** — Tie resource lifetime to object lifetime so cleanup is automatic
3. **Control Plane** — A single orchestrator (Kernal) that manages the lifecycle of all subsystems
4. **State Machine** — Track CREATED → INITIALIZED → STARTED → STOPPING → STOPPED
5. **Thread Ownership** — Each subsystem creates and joins its own threads
6. **Graceful Shutdown** — Signal → Unblock → Join → Cleanup
7. **Poison Pills** — Special messages that unblock threads waiting on queues
8. **Startup Rollback** — If one thing fails to start, stop everything that already started
9. **Shutdown Continuation** — During shutdown, keep going even if one thing fails

These aren't just patterns for this project. These are the fundamental patterns of systems engineering. Every database, every web server, every operating system uses these same concepts. You now understand them.
