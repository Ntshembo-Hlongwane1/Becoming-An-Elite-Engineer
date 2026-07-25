#include <iostream>
#include "parser.hpp"
#include "internal/kernal/core/headerfiles/sharedmutexs.hpp"


std::string Parser::Name() {
    return "Parser";
};

Error Parser::Init(){
    std::cout << "\n [" << Name() <<"] Initializing..." << std::endl;
    return Error("");
};

Error Parser::Start(){
    std::cout << "\n [" << Name() <<"] Starting..." << std::endl;
    return Error("");
};

Error Parser::Stop(){
    std::cout << "\n [" << Name() <<"] Stopping..." << std::endl;
    return Error("");

}


void Parser::Run(){
    std::cout << "\n [" << Name() << "] Running..." << std::endl;

    while(true){
        std::string token;
        parserQueue.pop_blocking(token);

        if (token.empty()) {   // poison pill
            std::cout << "[" << Name() << "] Received poison pill, exiting." << std::endl;
            break;
        }
        std::cout << "Token: " << token << std::endl;
    }
}