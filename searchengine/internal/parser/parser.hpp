#pragma once
#include "internal/kernal/core/headerfiles/subsystem.hpp"
#include "internal/kernal/core/datastructures/ringbuffer.hpp"
#include "internal/kernal/core/headerfiles/error.hpp"


class Parser : public Subsystem {

    public:
        Parser(RingBuffer<std::string, 1024>& parserQueue) : parserQueue(parserQueue){};
        std::string Name() override;
        Error Init() override;
        Error Start() override;
        Error Stop() override;
        void Run();

    private:
        RingBuffer<std::string, 1024>& parserQueue;


};