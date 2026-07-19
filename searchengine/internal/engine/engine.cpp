#include "internal/engine/engine.h"
#include <iostream>
#include "internal/store/store.h"
#include <string>
#include <algorithm>

Engine::Engine(Store* store) : store_(store) {}

std::string Engine::Name() {
    return "Search Engine";
}

Error Engine::Init() {
    std::cout << "\n [" << Name() << "] Initializing..." << std::endl;
    return Error("");
}

Error Engine::Start() {
    std::cout << "\n [" << Name() << "] Starting..." << std::endl;
    return Error("");
}

Error Engine::Stop() {
    std::cout << "\n [" << Name() << "] Stopping..." << std::endl;
    return Error("");
}

std::string Engine::Prompt() {
    std::string prompt;
    std::cout << "\n [" << Name() << "] Search: ";
    std::getline(std::cin, prompt);
    std::cout << "\n [" << Name() << "] Searching: " << prompt << std::endl;

    return prompt;
};


void Engine::V1(std::string& prompt){
    for (std::string& str : store_->GetDataFiles()){
        if (eualsIgnoreCase(getLeftPart(str), prompt)){
            std::cout << "Search results: " << str << std::endl;
            break;
        }
    }
}

void Engine::V2(std::string& prompt) {
    std::transform(prompt.begin(), prompt.end(), prompt.begin(),
    [](unsigned char c) {
        return std::tolower(c);
    });
    const auto& index = store_->GetSearchIndex();
    auto it = index.find(prompt);

    std::cout << "\n Search Results: " << std::endl;

    if (it == index.end()) {
        std::cout << "No results found." << std::endl;
        return;
    }

    for (const auto& doc : it->second){
        std::cout << doc << std::endl;
    };
}

bool Engine::eualsIgnoreCase(const std::string& a, const std::string& b){

    if (a.length() != b.length()){
        return false;
    };


    for (size_t i = 0; i < a.length(); i++){
        char lowerA = std::tolower(static_cast<unsigned char>(a[i]));
        char lowerB = std::tolower(static_cast<unsigned char>(b[i]));

        if (lowerA != lowerB){
            return false;
        }
    }

    return true;
};

std::string Engine::getLeftPart(std::string text){
    size_t pos = text.find('.');

    if (pos == std::string::npos){
        return text;
    };

    return text.substr(0, pos);
}

void Engine::Run() {
    std::string result = Prompt();
    
    V2(result);

    // So I can do something with the prompt value here withou having had made another copy in memory of prompt
}




