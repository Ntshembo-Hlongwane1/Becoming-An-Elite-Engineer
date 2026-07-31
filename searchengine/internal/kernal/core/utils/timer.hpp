#pragma once
#include <chrono>
#include <string>
#include "internal/kernal/core/utils/logger.hpp"

class ScopedTimer {
    std::chrono::steady_clock::time_point start_;
    std::string name_;
    bool reported_ = false;

    public:
        explicit ScopedTimer(std::string name = "Timer")
            : start_(std::chrono::steady_clock::now()), name_(std::move(name)){}

            long long elapsed_ms() const {
                auto end = std::chrono::steady_clock::now();
                return std::chrono::duration_cast<std::chrono::milliseconds>(end - start_).count();
            }

            // Logs the elapsed time once; further calls (including the destructor) are no-ops.
            void report() {
                if (reported_) return;
                reported_ = true;
                Log(name_, "Duration (ms): " + std::to_string(elapsed_ms()));
            }

            ~ScopedTimer(){
                report();
            };

};
