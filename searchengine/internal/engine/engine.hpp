#pragma once
#include "internal/kernal/core/headerfiles/subsystem.hpp"
#include "internal/kernal/core/headerfiles/error.hpp"
#include <string>
#include <thread>
#include <atomic>
#include "internal/store/store.hpp"
#include <string>

class Engine : public Subsystem {
    public:
        Engine();
        Engine(Store* store);

        std::string Name() override;
        Error Init() override;
        Error Start() override;
        Error Stop() override;
        void Run();   


    private:
        std::string Prompt();
        void V1(std::string& prompt);
        void V2(std::string& prompt);
        Store* store_;
        bool eualsIgnoreCase(const std::string& a, const std::string& b);
        std::string getLeftPart(std::string text);
};
