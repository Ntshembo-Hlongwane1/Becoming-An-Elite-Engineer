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