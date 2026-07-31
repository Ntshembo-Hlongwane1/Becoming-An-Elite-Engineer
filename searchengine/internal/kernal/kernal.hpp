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

        Error Register(SubsystemId id, Subsystem* subsystem);

        Error InitAll();

        Error StartAll();

        Error StopAll();

        Subsystem* GetSubsystem(SubsystemId id) const;


    private:
        std::map<SubsystemId, std::unique_ptr<Subsystem>> subsystems_; // ownership
        std::vector<SubsystemId> order_; // registration order

        bool HasError(const Error& error) const;

        Error StartSubSystem_(SubsystemId id);
};