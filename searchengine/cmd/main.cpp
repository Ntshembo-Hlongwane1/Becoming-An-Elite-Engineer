#include <iostream>
#include "internal/kernal/kernal.hpp"
#include "internal/engine/engine.hpp"
#include "internal/kernal/core/headerfiles/error.hpp"
#include "internal/filereader/filereader.hpp"
#include "internal/store/store.hpp"
#include "internal/lexer/lexer.hpp"

int main() {
    std::cout << "SonarSearch Starting..." << std::endl;

    Kernal kernal;

    Error storeRegError = kernal.Register("Store", new Store());

    if (!storeRegError.GetMessage().empty()) {
        std::cerr << "Store registration failed: " << storeRegError.GetMessage() << std::endl;
        return 1;
    }

    Error filereaderRegError = kernal.Register("File Reader", new FileReader(dynamic_cast<Store*>(kernal.GetSubsystem("Store"))));

    if (!filereaderRegError.GetMessage().empty()) {
        std::cerr << "File Reader registration failed: " << filereaderRegError.GetMessage() << std::endl;
        return 1;
    }

    std::unique_ptr<Lexer> lexer = std::make_unique<Lexer>();
    Error lexerRegError = kernal.Register("Lexer", lexer.release());

    if (!lexerRegError.GetMessage().empty()){
        std::cout << "Registration failed: " << lexerRegError.GetMessage() << std::endl;
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
    
    if (engine) {
        engine->Run();   // entry point
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