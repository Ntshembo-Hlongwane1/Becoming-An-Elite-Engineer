#include "file.hpp"
#include <iostream>
#include <string>
#include <fcntl.h>
#include <stdexcept>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <vector>

FileManager::FileManager(std::string& filePath) : fd_(-1), filePath_(filePath){
    bool isOpened = OpenFile_(filePath);
};

FileManager::~FileManager(){
    if (fd_ != -1){
        ::close(fd_);
        fd_ = -1;
    }
};

void FileManager::Write(std::vector<char>& data){
    if (fd_ == -1){
        throw std::runtime_error("Cannot write on closed file (" + filePath_ + ")");
    };

    Write_(data.data(), data.size());
};

[[nodiscard]] bool FileManager::OpenFile_(std::string& path){
    fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_DIRECT);

    if (fd_ == -1){
        throw std::runtime_error("Failed to open (" + path + ")" + strerror(errno));
    };
};

void FileManager::Write_(const char* data, size_t len){

}
