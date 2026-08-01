#pragma once
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>

// std::cout is race-free but not atomic: separate << insertions from different threads
// interleave mid-line. Compose the whole line first, then emit it under one lock.
// cout and cerr share the lock so the two streams cannot tear each other either.
inline std::mutex logMutex;

inline std::string FormatLogLine(std::string_view name, std::string_view message) {
    std::string line;
    line.reserve(name.size() + message.size() + 6);
    line += "\n[";
    line += name;
    line += "] ";
    line += message;
    line += "\n";
    return line;
}

inline void Log(std::string_view name, std::string_view message) {
    const std::string line = FormatLogLine(name, message);

    std::lock_guard<std::mutex> lock(logMutex);
    std::cout << line << std::flush;
}

inline void LogError(std::string_view name, std::string_view message) {
    const std::string line = FormatLogLine(name, message);

    std::lock_guard<std::mutex> lock(logMutex);
    std::cerr << line << std::flush;
}
