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