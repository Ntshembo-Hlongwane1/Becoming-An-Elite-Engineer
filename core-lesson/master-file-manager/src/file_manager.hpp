#pragma once

#include <cstdio>
#include <vector>
#include <string>
#include <chrono>

class FileManager {

    public:
        FileManager(const std::string& path);
        void Put(const std::vector<char>& chunk);
        void Read(std::vector<char>& out);
        ~FileManager() noexcept;

    private:
        int fd_;
        std::string path_;

        void write_chunk(const char* data, size_t len);
        static double seconds_since(const std::chrono::steady_clock::time_point& start);
        size_t ReadChunk(char* out, size_t capacity);
};
