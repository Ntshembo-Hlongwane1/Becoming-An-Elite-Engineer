// internal/parser/parser.hpp
#pragma once
#include "internal/kernal/core/headerfiles/subsystem.hpp"
#include "internal/kernal/core/headerfiles/indexresult.hpp"
#include "internal/kernal/core/datastructures/ringbuffer.hpp"
#include <thread>
#include <future>

class Parser : public Subsystem {
public:
    Parser(RingBuffer<std::string, 1024>& parserQueue, std::promise<IndexResult> donePromise)
        : parserQueue_(parserQueue), donePromise_(std::move(donePromise)) {}

    std::string Name() override;

protected:
    Error OnInit() override;
    Error OnStart() override;
    Error OnStop() override;

private:
    RingBuffer<std::string, 1024>& parserQueue_;
    std::thread run_thread_;
    std::promise<IndexResult> donePromise_;

    void Run();
};
