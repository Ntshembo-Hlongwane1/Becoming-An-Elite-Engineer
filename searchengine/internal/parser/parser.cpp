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