// internal/directoryreader/directoryreader.cpp
#include "directoryreader.hpp"
#include <iostream>
#include <filesystem>

std::string DirectoryReader::Name() { return "Dir Reader"; }

Error DirectoryReader::OnInit() {
    if (!std::filesystem::exists("data") || !std::filesystem::is_directory("data")) {
        return Error("Data directory not found");
    }
    return Error("");
}

Error DirectoryReader::OnStart() {
    running_.store(true, std::memory_order_release);
    
    run_thread_ = std::thread(&DirectoryReader::Run, this);
    
    return Error("");
}

Error DirectoryReader::OnStop() {
    running_.store(false, std::memory_order_release);
    

    if (run_thread_.joinable()) {
        run_thread_.join();
    }
    
    return Error("");
}

void DirectoryReader::Run() {
    std::cout << "\n [" << Name() << "] Scanning directory..." << std::endl;
    
    std::string dataPath = "data";
    
    for (const auto& entry : std::filesystem::directory_iterator(dataPath)) {
        // Check shutdown flag between iterations
        if (!running_.load(std::memory_order_acquire)) {
            std::cout << "\n [" << Name() << "] Shutdown during scan, aborting" << std::endl;
            break;
        }
        
        if (entry.is_regular_file()) {
            dirQueue_.push_blocking(entry.path().string());
        }
    }
    
    dirQueue_.push_blocking(std::string(""));
    
    std::cout << "\n [" << Name() << "] Scan complete" << std::endl;
}