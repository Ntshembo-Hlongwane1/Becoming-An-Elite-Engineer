// internal/parser/parser.cpp
#include "parser.hpp"
#include <iostream>
#include "internal/kernal/core/datastructures/bstree.hpp"
#include "internal/kernal/core/utils/logger.hpp"

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
    Log(Name(), "Processing started");
    
    int tokensProcessed = 0;
    
    while (running_.load(std::memory_order_acquire)) {
        std::string token;
        parserQueue_.pop_blocking(token);
        
        BSTree tree;

        if (token.empty()) {
            Log(Name(), "Received end signal");
            tree.print();
            break;
        }
        
        tree.insert(token);
        // Log(Name(), "Received Token: " + token);
    }
    
    Log(Name(), "Done. Tokens: " + std::to_string(tokensProcessed));
}