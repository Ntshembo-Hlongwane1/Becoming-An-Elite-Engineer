#pragma once
#include "internal/kernal/core/headerfiles/subsystem.hpp"
#include "internal/store/store.hpp"
#include "internal/kernal/core/datastructures/ringbuffer.hpp"
#include <string>

class DirectoryReader : public Subsystem{
    public:
        DirectoryReader(RingBuffer<std::string, 1024>& dirQueue) : dirQueue(dirQueue){};
        std::string Name() override;
        Error Init() override;
        Error Start() override;
        Error Stop() override;
        void Run();   


    private:
        RingBuffer<std::string, 1024>& dirQueue;
        std::string getRightPart(std::string str);
};