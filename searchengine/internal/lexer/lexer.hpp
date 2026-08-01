// internal/lexer/lexer.hpp
#pragma once
#include "internal/kernal/core/headerfiles/subsystem.hpp"
#include "internal/kernal/core/datastructures/ringbuffer.hpp"
#include "ilp.hpp"
#include <atomic>
#include <thread>
#include <vector>
#include <string>
#include <unordered_set>

class Lexer : public Subsystem {
public:
    Lexer(
        RingBuffer<std::string, 1024>& dirQueue,
        RingBuffer<ILP, 1024>& lineQueue,
        RingBuffer<std::string, 1024>& parserQueue)
        : dirQueue_(dirQueue), lineQueue_(lineQueue), parserQueue_(parserQueue) {}

    std::string Name() override;

protected:
    Error OnInit() override;
    Error OnStart() override;
    Error OnStop() override;

private:
    RingBuffer<std::string, 1024>& dirQueue_;
    RingBuffer<ILP, 1024>& lineQueue_;
    RingBuffer<std::string, 1024>& parserQueue_;

    std::thread run_thread_;
    static constexpr int KWorkerCount = 2;
    std::vector<std::thread> workers_;
    std::atomic<int> activeWorkers_{0};
    std::atomic<bool> downstreamClosed_{false};

    void Run();
    void Worker(std::string id);
    void CloseParserQueue_();

    std::vector<std::string_view> splitLine(std::string_view& line);
};
