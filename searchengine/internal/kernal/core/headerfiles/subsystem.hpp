#pragma once
#include <string>
#include <atomic>
#include <iostream>
#include "internal/kernal/core/headerfiles/error.hpp"
#include "internal/kernal/core/utils/logger.hpp"
#include <cstdint>

class Subsystem {
    public:
        enum class State {
            CREATED,
            INITIALIZED,
            STARTED,
            STOPPING,
            STOPPED
        };

        virtual ~Subsystem() {} // Clean up
        virtual std::string Name() = 0;
        
        Error Init() {
            if (state_ != State::CREATED){
                return Error("[" + Name() + "] Cannot Initialize not in CREATED state");
            }

            Log(Name(), "Initializing...");
            Error error = OnInit();

            if (error.GetMessage().empty()){
                state_ = State::INITIALIZED;
            }

            return error;
        };
        
        Error Start(){
            if (state_ != State::INITIALIZED){
                return Error("[" + Name() + "] Cannot Start - not in INITIALIZED state");
            }

            Log(Name(), "Starting...");
            Error error = OnStart();

            if (error.GetMessage().empty()){
                state_ = State::STARTED;
            }

            return error;
        };

        Error Stop() {
            if (state_ != State::STARTED) {
                Log(Name(), "Not started, skipping stop");
                return Error("");
            }
            Log(Name(), "Stopping...");
            state_ = State::STOPPING;
            Error err = OnStop();
            state_ = State::STOPPED;
            Log(Name(), "Stopped");
            return err;
        }

        State GetState() const { return state_; }

        bool IsRunning() const { return state_ == State::STARTED; }

        protected:
            virtual Error OnInit() = 0;
            virtual Error OnStart() = 0;
            virtual Error OnStop() = 0;
            std::atomic<bool> running_{false};

        private:
            State state_ = State::CREATED;
};

enum class SubsystemId : std::uint8_t {
    Store,
    DirReader,
    Lexer,
    Parser,
    Engine,
};

inline std::string_view ToString(SubsystemId id) {
    switch (id) {
        case SubsystemId::Store:     return "Store";
        case SubsystemId::DirReader: return "Dir Reader";
        case SubsystemId::Lexer:     return "Lexer";
        case SubsystemId::Parser:    return "Parser";
        case SubsystemId::Engine:    return "Search Engine";
    }
    return "UnknownSubsystem";
}

inline std::string ToStdString(SubsystemId id) {
    return std::string(ToString(id));
}