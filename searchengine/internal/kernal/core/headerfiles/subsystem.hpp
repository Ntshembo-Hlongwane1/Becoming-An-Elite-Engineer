#pragma once
#include <string>
#include <atomic>
#include <iostream>
#include "internal/kernal/core/headerfiles/error.hpp"

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

            std::cout << "\n [" << Name() << "] Initializing..." << std::endl;
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

            std::cout << "\n [" << Name() << "] Starting..." << std::endl;
            Error error = OnStart();

            if (error.GetMessage().empty()){
                state_ = State::STARTED;
            }

            return error;
        };

        Error Stop() {
            if (state_ != State::STARTED) {
                std::cout << "\n [" << Name() << "] Not started, skipping stop" << std::endl;
                return Error("");
            }
            std::cout << "\n [" << Name() << "] Stopping..." << std::endl;
            state_ = State::STOPPING;
            Error err = OnStop();
            state_ = State::STOPPED;
            std::cout << "\n [" << Name() << "] Stopped" << std::endl;
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