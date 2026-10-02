#pragma once
#include <string>
#include <chrono>
#include "internal/kernal/core/include/types/core.hpp"


struct UploadSessionMintRequest {
    UID uid;
    std::chrono::system_clock::time_point now;
};