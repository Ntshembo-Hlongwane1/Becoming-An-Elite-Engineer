// internal/engine/engine.cpp
#include "engine.hpp"
#include <iostream>
#include <algorithm>
#include "internal/kernal/core/utils/logger.hpp"

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

    
    if (run_thread_.joinable()) {
        run_thread_.join();
    }
    
    return Error("");
}

void Engine::Run() {
    Log(Name(), "Ready for queries");
    
    while (running_.load(std::memory_order_acquire)) {
        std::string query = Prompt();
        
        if (query.empty() || !running_.load()) {
            break;
        }
        
        Search(query);
    }
    
    Log(Name(), "Query loop exited");
}

std::string Engine::Prompt() {
    std::string prompt;
    Log(Name(), "Search: ");
    std::getline(std::cin, prompt);
    return prompt;
}

void Engine::Search(std::string& query) {
    std::transform(query.begin(), query.end(), query.begin(),
        [](unsigned char c) { return std::tolower(c); });
    
    const auto& index = store_->GetSearchIndex();
    auto it = index.find(query);
    
    Log(Name(), "Search Results: ");

    if (it == index.end()) {
        Log(Name(), "No results found.");
        return;
    }

    for (const auto& doc : it->second) {
        Log(Name(), doc);
    }
}