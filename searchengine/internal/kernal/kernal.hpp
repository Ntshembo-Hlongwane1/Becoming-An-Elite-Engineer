#pragma once

#include <map>
#include <string>
#include <vector>
#include <memory>

#include "internal/kernal/core/headerfiles/subsystem.hpp"
#include "internal/kernal/core/headerfiles/error.hpp"


class Kernal {
    public:
        Kernal() = default;

        Kernal(const Kernal&) = delete;

        Kernal& operator=(const Kernal&) = delete;

        Kernal(Kernal&&) = delete;
        
        Kernal& operator=(Kernal&&) = delete;

        ~Kernal();

        std::string GetName() const;

        Error Register(const std::string& name, Subsystem* subsystem);

        Error InitAll();

        Error StartAll();

        Error StopAll();

        Subsystem* GetSubsystem(const std::string& name) const;


    private:
        std::map<std::string, std::unique_ptr<Subsystem>> subsystems_; // ownership
        std::vector<std::string> order_; // registration order

        bool HasError(const Error& error) const;
};