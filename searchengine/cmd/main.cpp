// cmd/main.cpp
#include <iostream>
#include <csignal>
#include <thread>
#include <chrono>
#include "internal/kernal/kernal.hpp"
#include "internal/engine/engine.hpp"
#include "internal/parser/parser.hpp"
#include "internal/directoryreader/directoryreader.hpp"
#include "internal/store/store.hpp"
#include "internal/lexer/lexer.hpp"
#include "internal/kernal/core/datastructures/ringbuffer.hpp"
#include "internal/kernal/core/headerfiles/indexresult.hpp"
#include "internal/kernal/core/utils/logger.hpp"
#include <future>

namespace {
    volatile std::sig_atomic_t g_shutdown = 0;
}

extern "C" void HandleShutdownSignal(int) {
    g_shutdown = 1;
}

int main() {
    Log("SonarSearch", "Starting...");

    // ===== 1. Create the control plane =====
    Kernal kernal;

    // ===== 2. Create shared queues (owned by main) =====
    RingBuffer<std::string, 1024> dirQueue;
    RingBuffer<ILP, 1024> lineQueue;
    RingBuffer<std::string, 1024> parserQueue;

    // ===== 3. Create the completion channel =====
    // The future must be carved off before the promise is moved into the Parser.
    std::promise<IndexResult> indexPromise;
    std::future<IndexResult> indexFuture = indexPromise.get_future();

    // ===== 4. Register subsystems (Kernal takes ownership) =====
    // Order matters! Dependencies must be registered first.

    // Store: no dependencies, passive data store
    kernal.Register(SubsystemId::Store, new Store());

    // DirectoryReader: produces file paths → dirQueue
    kernal.Register(SubsystemId::DirReader, new DirectoryReader(dirQueue));

    // Lexer: consumes dirQueue, produces to lineQueue and parserQueue
    kernal.Register(SubsystemId::Lexer, new Lexer(dirQueue, lineQueue, parserQueue));

    // Parser: consumes parserQueue, reports completion through the promise
    kernal.Register(SubsystemId::Parser, new Parser(parserQueue, std::move(indexPromise)));

    // Engine: reads from Store
    Store* store = dynamic_cast<Store*>(kernal.GetSubsystem(SubsystemId::Store));
    kernal.Register(SubsystemId::Engine, new Engine(store));

    kernal.SetIndexFuture(std::move(indexFuture));

    // ===== 5. Install signal handlers before StartAll, so a long cold boot is
    //          interruptible while the Kernal is parked on the future =====
    std::signal(SIGINT, HandleShutdownSignal);
    std::signal(SIGTERM, HandleShutdownSignal);
    kernal.SetAbortCheck([] { return g_shutdown != 0; });

    // ===== 6. Initialize all (validate, allocate, pre-flight check) =====
    Error initError = kernal.InitAll();
    if (!initError.GetMessage().empty()) {
        LogError("SonarSearch", "Init failed: " + initError.GetMessage());
        return 1;
    }

    // ===== 7. Start all (create threads, begin processing) =====
    Error startError = kernal.StartAll();
    if (!startError.GetMessage().empty()) {
        LogError("SonarSearch", "Start failed: " + startError.GetMessage());
        kernal.StopAll();
        return 1;
    }

    Log("SonarSearch", "Running. Press Ctrl+C to exit...");
    while (!g_shutdown) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // ===== 8. Graceful shutdown =====
    Error stopError = kernal.StopAll();
    if (!stopError.GetMessage().empty()) {
        LogError("SonarSearch", "Shutdown errors:\n" + stopError.GetMessage());
        return 1;
    }

    Log("SonarSearch", "Stopped cleanly.");
    return 0;
}