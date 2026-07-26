// internal/engine/engine.hpp
#pragma once
#include "internal/kernal/core/headerfiles/subsystem.hpp"
#include "internal/store/store.hpp"
#include <thread>
#include <string>

class Engine : public Subsystem {
public:
    Engine(Store* store) : store_(store) {}
    std::string Name() override;

protected:
    Error OnInit() override;
    Error OnStart() override;
    Error OnStop() override;

private:
    Store* store_;              
    std::thread run_thread_;     
    
    void Run();
    std::string Prompt();
    void Search(std::string& query);
};