#pragma once 
#include <string>
#include <vector>
#include <chrono>

class FileManager {
    
    public:
        FileManager(std::string& filePath);
        ~FileManager();

        void Write(std::vector<char>& data);
        void WriteAt(std::vector<char>& data, off_t offset);

    private:
        int fd_;
        std::string filePath_;
        [[nodiscard]] bool OpenFile_(std::string& path);
        void Write_(const char* data, size_t len);
        void WriteAt_(const  char* data, size_t len, off_t offset);
        [[nodiscard]] double SecondsSince_(std::chrono::steady_clock::time_point start);

};