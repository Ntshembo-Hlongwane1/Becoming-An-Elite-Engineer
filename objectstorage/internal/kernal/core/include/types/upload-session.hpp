#pragma once 
#include <string>
#include <chrono>
#include "internal/kernal/core/include/types/core.hpp"


struct UploadSession {
    std::string uid;
    UploadSessionKey sessionKey;
    std::chrono::system_clock::time_point createdAt;
    std::chrono::system_clock::time_point expiresAt;
};
