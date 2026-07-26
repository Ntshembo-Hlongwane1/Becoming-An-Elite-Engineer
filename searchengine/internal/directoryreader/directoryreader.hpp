// internal/directoryreader/directoryreader.hpp
#pragma once
#include "internal/kernal/core/headerfiles/subsystem.hpp"
#include "internal/kernal/core/datastructures/ringbuffer.hpp"
#include <string>
#include <thread>

class DirectoryReader : public Subsystem {
public:
    DirectoryReader(RingBuffer<std::string, 1024>& dirQueue) 
        : dirQueue_(dirQueue) {}
    
    std::string Name() override;

protected:
    Error OnInit() override;
    Error OnStart() override;
    Error OnStop() override;

private:
    RingBuffer<std::string, 1024>& dirQueue_;  
    std::thread run_thread_;
    
    void Run(); 
};