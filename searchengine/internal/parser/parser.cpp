// internal/parser/parser.cpp
#include "parser.hpp"
#include <chrono>
#include <iostream>
#include "internal/kernal/core/datastructures/bstree.hpp"
#include "internal/kernal/core/utils/logger.hpp"

std::string Parser::Name() { return "Parser"; }

Error Parser::OnInit() {
    return Error("");
}

Error Parser::OnStart() {
    running_.store(true, std::memory_order_release);

    try {
        run_thread_ = std::thread(&Parser::Run, this);
    } catch (const std::system_error& e) {
        running_.store(false, std::memory_order_release);
        return Error("Thread creation failed: " + std::string(e.what()));
    }

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

    BSTree tree;
    const auto startTime = std::chrono::steady_clock::now();
    IndexResult result;
    bool drained = false;

    try {
        while (running_.load(std::memory_order_acquire)) {
            std::string token;
            parserQueue_.pop_blocking(token);

            if (token.empty()) {
                Log(Name(), "Received end signal");
                drained = true;
                break;
            }

            tree.insert(token);
            result.tokensIndexed++;
        }

        // Exited on running_ == false: an abort, not a completion. Leaving the promise
        // unfulfilled makes ~promise deliver broken_promise, so the Kernal is told the
        // truth instead of being handed a partial index labelled "success".
        if (!drained) {
            Log(Name(), "Stopped before end of stream; indexing incomplete");
            return;
        }

        if (result.tokensIndexed == 0) {
            throw IndexingError("corpus produced zero tokens");
        }

        result.elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime);

        donePromise_.set_value(result);

    } catch (...) {
        // An exception escaping a thread function is std::terminate. set_exception
        // carries it to the Kernal instead, to be rethrown there on get().
        donePromise_.set_exception(std::current_exception());
    }

    Log(Name(), "Done. Tokens: " + std::to_string(result.tokensIndexed));
}
