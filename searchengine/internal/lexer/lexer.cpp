// internal/lexer/lexer.cpp
#include "lexer.hpp"
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include "internal/kernal/core/utils/stopwords.cpp"
#include "internal/kernal/core/utils/logger.hpp"
#include "internal/kernal/core/headerfiles/token.hpp"

std::string Lexer::Name() { return "Lexer"; }

Error Lexer::OnInit() {
    // Could pre-load stop word lists here
    return Error("");
}

Error Lexer::OnStart() {
    running_.store(true, std::memory_order_release);
    downstreamClosed_.store(false, std::memory_order_release);

    // Must be set before the workers exist, or a fast worker could decrement it
    // past zero and take the last-one-out branch while others are still running.
    activeWorkers_.store(KWorkerCount, std::memory_order_release);

    try {
        workers_.reserve(KWorkerCount);
        for (int i = 0; i < KWorkerCount; ++i) {
            workers_.emplace_back(&Lexer::Worker, this, std::to_string(i + 1));
        }

        run_thread_ = std::thread(&Lexer::Run, this);
    } catch (const std::system_error& e) {
        running_.store(false, std::memory_order_release);

        // Only the workers that actually spawned will consume a pill.
        activeWorkers_.store(static_cast<int>(workers_.size()), std::memory_order_release);
        for (size_t i = 0; i < workers_.size(); ++i) {
            lineQueue_.push_blocking(ILP{"", ""});
        }

        for (auto& worker : workers_) {
            if (worker.joinable()) worker.join();
        }
        workers_.clear();

        if (run_thread_.joinable()) run_thread_.join();

        CloseParserQueue_();

        return Error("Thread creation failed: " + std::string(e.what()));
    }

    return Error("");
}

Error Lexer::OnStop() {
    // Downstream pills are no longer pushed from here. Each thread forwards its own
    // end-of-stream marker on the way out of its loop, so normal completion and abort
    // take the same path. Stop only has to: signal, unblock our own input, and join.
    running_.store(false, std::memory_order_release);

    dirQueue_.push_blocking(std::string(""));

    // Run() must be joined first: on its way out it pushes the pills that free the workers.
    if (run_thread_.joinable()) {
        run_thread_.join();
    }

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();

    // No-op if a worker already closed it; covers the case where none ever ran.
    CloseParserQueue_();

    Log(Name(), "All threads joined");
    return Error("");
}

void Lexer::CloseParserQueue_() {
    bool expected = false;
    if (downstreamClosed_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        parserQueue_.push_blocking(std::string(""));
        Log(Name(), "Downstream (Parser) queue closed");
    }
}

void Lexer::Run() {
    Log(Name(), "Coordinator started");
    
    int filesProcessed = 0;
    
    while (running_.load(std::memory_order_acquire)) {
        std::string file;
        dirQueue_.pop_blocking(file);
        
        // Check for poison pill (from DirectoryReader or from our own Stop)
        if (file.empty()) {
            Log(Name(), "Coordinator received end signal");
            break;
        }
        
        // Open and read the file
        FILE* fp = fopen(file.c_str(), "r");
        if (!fp) {
            LogError(Name(), "Failed to open: " + file);
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
            
            // Both fields are copied into the queue slot. `file` is reassigned on the
            // next pop and `line` is overwritten by the next fgets, so nothing here can
            // be borrowed by a worker that pops later.
            lineQueue_.push_blocking(ILP{file, std::string(line)});
        }
        
        if (ferror(fp)) {
            LogError(Name(), "Read error on: " + file);
        }
        
        fclose(fp);
        filesProcessed++;
    }

    // One pill per worker: a pill is consumed by whichever worker pops it, not broadcast.
    // Runs on both the drained path and the abort path.
    for (int i = 0; i < KWorkerCount; ++i) {
        lineQueue_.push_blocking(ILP{"", ""});
    }

    Log(Name(), "Coordinator done. Files: " + std::to_string(filesProcessed));
}

void Lexer::Worker(std::string id) {
    Log(Name(), "Worker " + id + " started");
    
    int tokensProcessed = 0;
    
    while (running_.load(std::memory_order_acquire)) {
        ILP line;
        lineQueue_.pop_blocking(line);
        
        // Check for poison pill
        if (line.filepath.empty() && line.line.empty()) {
            Log(Name(), "Worker " + id + " received shutdown signal");
            break;
        }
        

        // Viewing the owned buffer is safe here: `line` outlives every token below.
        // (std::string::substr would return a temporary, so a view of it would dangle.)
        const std::string_view lineView(line.line);

        // Separators delimit tokens, they don't terminate them: the last word on a line
        // has no space after it. A loop that only fires on a found space therefore drops
        // one token per line. Treating end-of-line as a virtual separator gives every
        // token, first and last, the same single emit path.
        size_t start = 0;

        while (start <= lineView.size()) {
            const size_t sep  = lineView.find(' ', start);
            const size_t stop = (sep == std::string_view::npos) ? lineView.size() : sep;

            std::string_view token = lineView.substr(start, stop - start);

            std::string cleanedToken;
            cleanedToken.reserve(token.size());

            for (unsigned char c : token){
                if (!std::isspace(c) && !std::ispunct(c)){
                    cleanedToken.push_back(std::tolower(static_cast<unsigned char>(c)));
                }
            }

            if (!cleanedToken.empty() && !StopWords::isStopWord(cleanedToken)){
                parserQueue_.push_blocking(cleanedToken);
                tokensProcessed++;
            }

            if (sep == std::string_view::npos) break;
            start = sep + 1;
        }

    }

    Log(Name(), "Worker " + id + " done. Tokens: " + std::to_string(tokensProcessed));

    // fetch_sub returns the value before subtracting, so seeing 1 means this thread took
    // the count to zero and is the last worker out. Only it closes the downstream queue,
    // and only after every worker has finished pushing (the release half of acq_rel).
    if (activeWorkers_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        CloseParserQueue_();
    }
}

std::vector<std::string_view> Lexer::splitLine(std::string_view& line) {
    std::vector<std::string_view> result;
    std::size_t start = 0;
    std::size_t end = line.find(' ');

    while (end != std::string::npos) {
        result.emplace_back(line.substr(start, end - start));
        start = end + 1;
        end = line.find(' ', start);
    }
    result.emplace_back(line.substr(start));
    return result;
}