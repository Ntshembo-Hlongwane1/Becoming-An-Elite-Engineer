#include "file.hpp"
#include <iostream>
#include <string>
#include <fcntl.h>
#include <stdexcept>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <vector>
#include <chrono>

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

void FileManager::WriteAt(std::vector<char>& data, off_t offset){
    if (fd_ == -1){
        throw std::runtime_error("Cannot write on closed file (" + filePath_ + ")" + strerror(errno));
    }

    WriteAt_(data.data(), data.size(), offset);
}

[[nodiscard]] bool FileManager::OpenFile_(std::string& path){
    fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_DIRECT, 0644);

    if (fd_ == -1){
        throw std::runtime_error("Failed to open (" + path + ")" + strerror(errno));
    };

    return true;
};

void FileManager::Write_(const char* data, size_t len){
    size_t done = 0;

    auto start = std::chrono::steady_clock::now();

    while (done < len){

        ssize_t n = ::write(fd_, data + done, len - done);

        if (n < 0){
            if (errno == EINTR){
                continue;
            }

            throw std::runtime_error("Write to: (" + filePath_ + ") failed" + strerror(errno));
        }

        if (n == 0){
            throw std::runtime_error("Write (" + filePath_ +") returned 0" + strerror(errno));
        }

        done += static_cast<size_t>(n);
    }

    auto elapsed = SecondsSince_(start);

    std::cout << "Elapsed: " << elapsed << std::endl;
}


void FileManager::WriteAt_(const char* data, size_t len, off_t offset) {

    auto start = std::chrono::steady_clock::now();

    if (::lseek(fd_, offset, SEEK_SET) == -1){
        throw std::runtime_error("lseek (" + filePath_ + ") failed" + strerror(errno));
    };

    size_t done = 0;

    while (done < len){
        ssize_t n = ::write(fd_, data + done, len - done);

        if (n < 0){
            if (errno == EINTR){
                continue;
            }

            throw std::runtime_error("Write failed (" + filePath_ + ")" + strerror(errno));
        };

        if (n == 0){
            throw std::runtime_error("Write on (" + filePath_ + ") returned 0" + strerror(errno));
        };

        done += static_cast<size_t>(n);
    };

    double elapsed = SecondsSince_(start);

    std::cout << "Write completed in: " << elapsed << std::endl;
}

[[nodiscard]] double FileManager::SecondsSince_(std::chrono::steady_clock::time_point start){
    auto end = std::chrono::steady_clock::now();

    return std::chrono::duration<double>(end - start).count();
};