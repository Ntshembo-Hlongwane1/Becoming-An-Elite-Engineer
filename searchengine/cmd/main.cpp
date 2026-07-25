#include <iostream>
#include "internal/kernal/kernal.hpp"
#include "internal/engine/engine.hpp"
#include "internal/parser/parser.hpp"
#include "internal/kernal/core/headerfiles/error.hpp"
#include "internal/kernal/core/headerfiles/token.hpp"
#include "internal/directoryreader/directoryreader.hpp"
#include "internal/store/store.hpp"
#include "internal/lexer/lexer.hpp"
#include "internal/kernal/core/datastructures/ringbuffer.hpp"
#include <thread>

int main() {
    std::cout << "SonarSearch Starting..." << std::endl;

    Kernal kernal;

    Error storeRegError = kernal.Register("Store", new Store());

    if (!storeRegError.GetMessage().empty()) {
        std::cerr << "Store registration failed: " << storeRegError.GetMessage() << std::endl;
        return 1;
    };

    RingBuffer<std::string, 1024> dirQueue;
    RingBuffer<ILP, 1024> lineQueue;
    RingBuffer<std::string, 1024> parserQueue;

    std::unique_ptr<Lexer> lexer = std::make_unique<Lexer>(dirQueue, lineQueue, parserQueue);
    Error lexerRegError = kernal.Register("Lexer", lexer.release());

    if (!lexerRegError.GetMessage().empty()){
        std::cout << "Registration failed: " << lexerRegError.GetMessage() << std::endl;
    }


    Error dirReaderRegError = kernal.Register("Dir Reader", new DirectoryReader(dirQueue));

    if (!dirReaderRegError.GetMessage().empty()) {
        std::cerr << "Dir Reader registration failed: " << dirReaderRegError.GetMessage() << std::endl;
        return 1;
    }

  
    Error parserRegError = kernal.Register("Parser", new Parser(parserQueue));

    if (!parserRegError.GetMessage().empty()) {
        std::cerr << "Parser registration failed: " << dirReaderRegError.GetMessage() << std::endl;
        return 1;
    }


    Error regError = kernal.Register("Search Engine", new Engine(dynamic_cast<Store*>(kernal.
  GetSubsystem("Store"))));

    if (!regError.GetMessage().empty()) {
        std::cerr << "Registration failed: " << regError.GetMessage() << std::endl;
        return 1;
    }


    Error initError = kernal.InitAll();
    if (!initError.GetMessage().empty()) {
        std::cerr << "Initialization failed: " << initError.GetMessage() << std::endl;
        return 1;
    }

    Error startError = kernal.StartAll();
    if (!startError.GetMessage().empty()) {
        std::cerr << "Startup failed: " << startError.GetMessage() << std::endl;
        return 1;
    }

    Engine* engine = dynamic_cast<Engine*>(kernal.GetSubsystem("Search Engine"));
    DirectoryReader* dirReader = dynamic_cast<DirectoryReader*>(kernal.GetSubsystem("Dir Reader"));
    Lexer* lx = dynamic_cast<Lexer*>(kernal.GetSubsystem("Lexer"));
    Parser* parser = dynamic_cast<Parser*>(kernal.GetSubsystem("Parser"));


    
    if (engine) {
        std::thread lexer_thread(&Lexer::Run, lx);
        std::thread dir_reader_thread(&DirectoryReader::Run, dirReader);
        std::thread parser_thread(&Parser::Run, parser);

        // engine->Run();   // entry point

        if (lexer_thread.joinable()) {
            lexer_thread.join();  // Wait for Lexer to finish
        }
        
        if (dir_reader_thread.joinable()) {
            dir_reader_thread.join();  // Wait for DirReader to finish
        }
        
        if (parser_thread.joinable()){
            parser_thread.join();
        }

    } else {
        std::cerr << "Engine not found!" << std::endl;
        return 1;
    }


    std::cout << "\nSonarSearch Running. Press Enter to exit..." << std::endl;
    std::cin.get(); // Wait for user input

    // 5. Cleanly stop subsystems on exit
    Error stopError = kernal.StopAll();
    if (!stopError.GetMessage().empty()) {
        std::cerr << "Shutdown encountered errors:\n" << stopError.GetMessage() << std::endl;
        return 1;
    }
    
    return 0;
}