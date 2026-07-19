#include <iostream>
#include "internal/kernal/kernal.h"
#include "internal/engine/engine.h"
#include "internal/kernal/core/headerfiles/error.h"
#include "internal/filereader/filereader.h"
#include "internal/store/store.h"

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