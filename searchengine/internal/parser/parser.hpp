// internal/parser/parser.hpp
#pragma once
#include "internal/kernal/core/headerfiles/subsystem.hpp"
#include "internal/kernal/core/datastructures/ringbuffer.hpp"
#include <thread>

class Parser : public Subsystem {
public:
    Parser(RingBuffer<std::string, 1024>& parserQueue) 
        : parserQueue_(parserQueue) {}
    
    std::string Name() override;

protected:
    Error OnInit() override;
    Error OnStart() override;
    Error OnStop() override;

private:
    RingBuffer<std::string, 1024>& parserQueue_;
    std::thread run_thread_;
    
    void Run();
};
