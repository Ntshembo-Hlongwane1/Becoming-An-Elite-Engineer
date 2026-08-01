#pragma once
#include <chrono>
#include <cstddef>
#include <stdexcept>
#include <string>

// Reported by the Parser to the Kernal when a cold-boot indexing pass finishes.
// Moved across a thread boundary via std::promise/std::future, so it owns nothing shared.
struct IndexResult {
    std::size_t tokensIndexed = 0;
    std::size_t documentsIndexed = 0;
    std::chrono::milliseconds elapsedTime{0};
};

class IndexingError : public std::runtime_error {
public:
    explicit IndexingError(const std::string& what) : std::runtime_error(what) {}
};
