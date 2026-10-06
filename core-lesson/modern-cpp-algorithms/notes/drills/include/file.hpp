#pragma once 
#include <string>
#include <vector>

class FileManager {
    
    public:
        FileManager(std::string& filePath);
        ~FileManager();

        void Write(std::vector<char>& data);

    private:
        int fd_;
        std::string filePath_;
        [[nodiscard]] bool OpenFile_(std::string& path);
        void Write_(const char* data, size_t len);

};