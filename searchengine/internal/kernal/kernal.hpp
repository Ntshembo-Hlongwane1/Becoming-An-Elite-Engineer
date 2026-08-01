#pragma once

#include <functional>
#include <future>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "internal/kernal/core/headerfiles/subsystem.hpp"
#include "internal/kernal/core/headerfiles/error.hpp"
#include "internal/kernal/core/headerfiles/indexresult.hpp"


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

        // Read end of the Parser's completion channel. Must be wired before StartAll()
        // takes the cold path.
        void SetIndexFuture(std::future<IndexResult> indexReady);

        // Polled while waiting on a cold boot so a shutdown signal can break the wait.
        void SetAbortCheck(std::function<bool()> abortCheck);


    private:
        std::map<SubsystemId, std::unique_ptr<Subsystem>> subsystems_; // ownership
        std::vector<SubsystemId> order_; // registration order

        std::future<IndexResult> indexReady_;
        std::function<bool()> abortCheck_;

        bool HasError(const Error& error) const;

        Error StartSubSystem_(SubsystemId id);

        Error AwaitIndexing_();
};
