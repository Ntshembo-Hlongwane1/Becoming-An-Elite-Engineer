#pragma once
#include <string>
#include "internal/kernal/core/headerfiles/error.hpp"

class Subsystem {
    public:
        virtual ~Subsystem() {} // Clean up
        virtual std::string Name() = 0;
        virtual Error Init() = 0;
        virtual Error Start() = 0;
        virtual Error Stop() = 0;
};