#include "file_manager.hpp"
#include <vector>
#include <fcntl.h>
#include <stdexcept>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <cstring>

FileManager::FileManager(const std::string& path) : fd_(-1), path_(path) {
    fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);

    if (fd_ == -1){
        throw std::runtime_error("Failed to open (" + path_ + ")" + strerror(errno));
    };
}

FileManager::~FileManager() noexcept {
    if (fd_ != -1){
        ::close(fd_);
        fd_ = -1;
    }
}

void FileManager::Put(const std::vector<char>& chunk){

    if (fd_ == -1){
        throw std::runtime_error("WriteChunk on close file: (" + path_ + ")");
    }

    write_chunk(chunk.data(), chunk.size());
};

void FileManager::write_chunk(const char* data, size_t len){

    size_t done = 0; // Tracks how many bytes have been written so far

    auto start = std::chrono::steady_clock::now();

    while (done < len){
        /*
            - data + done --> Pointer arithmetic. data points to byte 0. data + done points to byte done. This is the next byte to write
            - len - done --> How many bytes are still left to write
        */
        ssize_t n = ::write(fd_, data + done, len - done);

        /*
            - n < 0 --> Means that write failed
            - errno was the set by the kernel to a specific error code
            - errno == EINTR --> "Interrupted by a signal" This is not a real failure. A signal arrived while we were blocked in write, and the kernal gave up early. The correct response is to retry, so continue jumps back to the top of the while loop
            - Any other error is fatal
        */
        if (n < 0){
            if (errno == EINTR){
                continue;
            }

            throw std::runtime_error("Write (" + path_ + "): " + std::strerror(errno));
        }


        /*
            - write returning 0 means "I wrote zero bytes" For a regular file with len - done > 0, this should never happen.
            - If we did not check this, the loop would spin forever: done would never grow, done < len would stay true
            - Defensive programming: fail loudly instead of hanging
         */
        if (n == 0){
            throw std::runtime_error("Write (" + path_ + ") returned 0 unexpectedly");
        }

        done += static_cast<size_t>(n);
    }

    double elapsedTime = seconds_since(start);

    std::cout << "Write completed in: " << elapsedTime << "seconds" << std::endl; 

}

void FileManager::Read(std::vector<char>& out){
    out.clear();
    if (::lseek(fd_, 0, SEEK_SET) == -1) {
        throw std::runtime_error("lseek (" + path_ + "): " + std::strerror(errno));
    }

    constexpr size_t kChunk = 64 * 1024;
    std::vector<char> buffer(kChunk);

    while (true) {
        size_t got = ReadChunk(buffer.data(), buffer.size());

        if (got == 0){
            break;
        };
        out.insert(out.end(), buffer.begin(), buffer.begin() + got);
    }
}

size_t FileManager::ReadChunk(char* out, size_t capacity){
    if (fd_ == -1){
        throw std::runtime_error("Read on closed file: ("+ path_+ ")");
    };

    while (true){
        ssize_t n = ::read(fd_, out, capacity);

        if (n < 0){
            if (errno == EINTR){
                continue;
            }

            throw std::runtime_error("Read (" + path_ + ")" + strerror(errno));
        }

        return static_cast<size_t>(n);
    }
}

double FileManager::seconds_since(const std::chrono::steady_clock::time_point& start){
    auto end = std::chrono::steady_clock::now();

    return std::chrono::duration<double>(end - start).count();
}