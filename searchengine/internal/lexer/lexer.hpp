#pragma once
#include "internal/kernal/core/headerfiles/error.hpp"
#include "internal/kernal/core/headerfiles/subsystem.hpp"
#include "internal/kernal/core/headerfiles/token.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include "internal/kernal/core/datastructures/ringbuffer.hpp"
#include "ilp.hpp"
#include <thread>
#include <condition_variable>
#include <vector>
#include <string_view>
#include <unordered_set>

class Lexer : public Subsystem {

    public:
        
        Lexer(
            RingBuffer<std::string, 1024>& dirQueue,
            RingBuffer<ILP, 1024>& lineQueue, 
            RingBuffer<std::string, 1024>& parserQueue) 
                : dirQueue(dirQueue), lineQueue(lineQueue), parserQueue(parserQueue){}

        std::string Name() override;
        Error Init() override;
        Error Start() override;
        Error Stop() override;
        void Run();   
        void SetFile(const std::string& filename);

    private:
        FILE* file;
        char line[456];
        size_t line_count = 0;
        std::string filepath;
        RingBuffer<std::string, 1024>& dirQueue;
        RingBuffer<ILP, 1024>& lineQueue;
        RingBuffer<std::string, 1024>& parserQueue;
        void worker(std::string thread);
        std::thread worker1;
        std::thread worker2;
        std::atomic<bool> running{true};
        bool isStopWord(std::string& word, std::unordered_set<std::string>& stopWord);
        std::vector<std::string> splitLine(std::string& line);
};