// internal/directoryreader/directoryreader.cpp
#include "directoryreader.hpp"
#include <iostream>
#include <filesystem>
#include "internal/kernal/core/utils/logger.hpp"

std::string DirectoryReader::Name() { return "Dir Reader"; }

namespace {
    // Absolute location of the document corpus.
    //
    // This lives on C: (SSD) rather than beside the executable on D: (HDD),
    // because a 100k-file scan is seek-bound and the drive it sits on dominates
    // startup cost far more than anything in this pipeline.
    //
    // Being absolute also decouples the scan from the process working
    // directory, so the engine and the benchmark harness read the same corpus
    // no matter where they are launched from.
    const char* const kCorpusPath = "C:/data";
}

Error DirectoryReader::OnInit() {
    if (!std::filesystem::exists(kCorpusPath) || !std::filesystem::is_directory(kCorpusPath)) {
        return Error(std::string("Data directory not found: ") + kCorpusPath);
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
    Log(Name(), "Scanning directory...");

    // Iterating an absolute root makes entry.path() absolute too, so the paths
    // handed to the Lexer resolve on their own. The Lexer's fopen() is
    // unchanged - it simply receives "C:/data/x.txt" instead of "data/x.txt".
    for (const auto& entry : std::filesystem::directory_iterator(kCorpusPath)) {
        // Check shutdown flag between iterations
        if (!running_.load(std::memory_order_acquire)) {
            Log(Name(), "Shutdown during scan, aborting");
            break;
        }
        
        if (entry.is_regular_file()) {
            dirQueue_.push_blocking(entry.path().string());
        }
    }
    
    dirQueue_.push_blocking(std::string(""));
    
    Log(Name(), "Scan complete");
}