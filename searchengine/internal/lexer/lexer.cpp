// internal/lexer/lexer.cpp
#include "lexer.hpp"
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include "internal/kernal/core/utils/stopwords.hpp"

std::string Lexer::Name() { return "Lexer"; }

Error Lexer::OnInit() {
    // Could pre-load stop word lists here
    return Error("");
}

Error Lexer::OnStart() {
    running_.store(true, std::memory_order_release);
    
    try {
        worker1_ = std::thread(&Lexer::Worker, this, "1");
        worker2_ = std::thread(&Lexer::Worker, this, "2");
        
        run_thread_ = std::thread(&Lexer::Run, this);
    } catch (const std::system_error& e) {
        running_.store(false, std::memory_order_release);
        
        // Send poison pills to any workers that started
        lineQueue_.push_blocking(ILP{"", ""});
        lineQueue_.push_blocking(ILP{"", ""});
        
        if (worker1_.joinable()) worker1_.join();
        if (worker2_.joinable()) worker2_.join();
        if (run_thread_.joinable()) run_thread_.join();
        
        return Error("Thread creation failed: " + std::string(e.what()));
    }
    
    return Error("");
}

Error Lexer::OnStop() {
    // === STEP 1: Signal all threads to stop ===
    running_.store(false, std::memory_order_release);
    
    // === STEP 2: Unblock the Run thread ===
    dirQueue_.push_blocking(std::string(""));
    
    // === STEP 3: Wait for the Run thread to exit ===
    if (run_thread_.joinable()) {
        run_thread_.join();
    }
    
    // === STEP 4: Unblock worker threads ===
    lineQueue_.push_blocking(ILP{"", ""});  // For worker 1
    lineQueue_.push_blocking(ILP{"", ""});  // For worker 2
    
    // === STEP 5: Wait for workers to exit ===
    if (worker1_.joinable()) {
        worker1_.join();
    }
    if (worker2_.joinable()) {
        worker2_.join();
    }
    
    // === STEP 6: Signal downstream (Parser) that we're done ===
    parserQueue_.push_blocking(std::string(""));
    
    std::cout << "\n [" << Name() << "] All threads joined" << std::endl;
    return Error("");
}

void Lexer::Run() {
    std::cout << "\n [" << Name() << "] Coordinator started" << std::endl;
    
    int filesProcessed = 0;
    
    while (running_.load(std::memory_order_acquire)) {
        std::string file;
        dirQueue_.pop_blocking(file);
        
        // Check for poison pill (from DirectoryReader or from our own Stop)
        if (file.empty()) {
            std::cout << "\n [" << Name() << "] Coordinator received end signal" << std::endl;
            break;
        }
        
        // Open and read the file
        FILE* fp = fopen(file.c_str(), "r");
        if (!fp) {
            std::cerr << "\n [" << Name() << "] Failed to open: " << file << std::endl;
            continue;
        }
        
        char line[456];
        while (fgets(line, sizeof(line), fp) != nullptr) {
            // Check shutdown between lines
            if (!running_.load(std::memory_order_acquire)) {
                break;
            }
            
            size_t len = strlen(line);
            if (len > 0 && line[len - 1] == '\n') {
                line[len - 1] = '\0';
            }
            
            lineQueue_.push_blocking({file, std::string(line)});
        }
        
        if (ferror(fp)) {
            std::cerr << "\n [" << Name() << "] Read error on: " << file << std::endl;
        }
        
        fclose(fp);
        filesProcessed++;
    }
    
    std::cout << "\n [" << Name() << "] Coordinator done. Files: " << filesProcessed << std::endl;
}

void Lexer::Worker(std::string id) {
    std::cout << "\n [" << Name() << "] Worker " << id << " started" << std::endl;
    
    int tokensProcessed = 0;
    
    while (running_.load(std::memory_order_acquire)) {
        ILP line;
        lineQueue_.pop_blocking(line);
        
        // Check for poison pill
        if (line.filepath.empty() && line.line.empty()) {
            std::cout << "\n [" << Name() << "] Worker " << id 
                      << " received shutdown signal" << std::endl;
            break;
        }
        
        // Tokenize and process the line
        auto tokens = splitLine(line.line);
        for (auto& token : tokens) {
            // Remove punctuation and whitespace
            token.erase(
                std::remove_if(token.begin(), token.end(),
                    [](unsigned char c) {
                        return std::isspace(c) || std::ispunct(c);
                    }
                ),
                token.end()
            );
            
            // Skip empty tokens and stop words
            if (!token.empty() && StopWords::isStopWord(token)) {
                parserQueue_.push_blocking(token);
                tokensProcessed++;
            }
        }
    }
    
    std::cout << "\n [" << Name() << "] Worker " << id << " done. Tokens: " 
              << tokensProcessed << std::endl;
}

std::vector<std::string> Lexer::splitLine(std::string& line) {
    std::vector<std::string> result;
    std::size_t start = 0;
    std::size_t end = line.find(' ');

    while (end != std::string::npos) {
        result.push_back(line.substr(start, end - start));
        start = end + 1;
        end = line.find(' ', start);
    }
    result.push_back(line.substr(start));
    return result;
}