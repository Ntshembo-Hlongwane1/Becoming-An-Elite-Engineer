#include "kernal.hpp"
#include <chrono>
#include <iostream>
#include "internal/store/store.hpp"
#include "internal/kernal/core/headerfiles/subsystem.hpp"
#include "internal/kernal/core/headerfiles/indexresult.hpp"
#include "internal/kernal/core/utils/logger.hpp"

namespace {
    constexpr auto kIndexTimeout   = std::chrono::minutes(20);
    constexpr auto kIndexHeartbeat = std::chrono::seconds(5);
}

Kernal::~Kernal(){
    for (auto it = order_.rbegin(); it != order_.rend(); ++it) {
        Subsystem* sub = subsystems_[*it].get();
        if (sub->GetState() == Subsystem::State::STARTED) {
            LogError(GetName(), "WARNING: " + ToStdString(*it)
                              + " was not stopped before Kernal destruction. Stopping now.");
            sub->Stop();
        }
    }
}; 

std::string Kernal::GetName() const {
    return "Kernal";
}

bool Kernal::HasError(const Error& error) const {
    return !error.GetMessage().empty();
}

Error Kernal::Register(SubsystemId id, Subsystem* subsystem) {
    const std::string name = ToStdString(id);

    if (subsystems_.find(id) != subsystems_.end()) {
        delete subsystem; // avoid leak
        return Error("[" + name + "] Subsystem already registered");
    }

    subsystems_[id] = std::unique_ptr<Subsystem>(subsystem);
    order_.push_back(id);

    Log(GetName(), "[" + name + "] Subsystem registered");
    return Error("");
}

Error Kernal::InitAll() {
    Log(GetName(), "Initializing all subsystems...");

    for (const auto& id : order_) {
        const std::string name = ToStdString(id);
        Subsystem* subsystem = subsystems_[id].get();
        Error error = subsystem->Init();

        if (HasError(error)) {
            Log(GetName(), "[" + name + "] Initialization failed: " + error.GetMessage());
            return Error("[" + name + "] Initialization failed: " + error.GetMessage());
        }

        Log(GetName(), "[" + name + "] Initialization successful");
    }

    Log(GetName(), "All subsystems initialized successfully");
    return Error("");
}

void Kernal::SetIndexFuture(std::future<IndexResult> indexReady) {
    indexReady_ = std::move(indexReady);
}

void Kernal::SetAbortCheck(std::function<bool()> abortCheck) {
    abortCheck_ = std::move(abortCheck);
}

Error Kernal::AwaitIndexing_() {
    if (!indexReady_.valid()) {
        return Error("No indexing future was wired; cannot await completion");
    }

    Log(GetName(), "Waiting for indexing to complete...");

    const auto deadline = std::chrono::steady_clock::now() + kIndexTimeout;

    while (indexReady_.wait_for(kIndexHeartbeat) != std::future_status::ready) {
        if (abortCheck_ && abortCheck_()) {
            return Error("Shutdown requested during indexing");
        }

        if (std::chrono::steady_clock::now() >= deadline) {
            return Error("Indexing timed out; the pipeline did not signal completion");
        }

        Log(GetName(), "Still indexing...");
    }

    // get(), not wait(): only get() rethrows what the Parser stored.
    try {
        const IndexResult result = indexReady_.get();

        Log(GetName(), "Indexing complete: " + std::to_string(result.tokensIndexed)
                     + " tokens in " + std::to_string(result.elapsedTime.count()) + " ms");
        return Error("");

    } catch (const IndexingError& error) {
        return Error(std::string("Indexing failed: ") + error.what());
    } catch (const std::future_error& error) {
        return Error(std::string("Parser did not complete indexing: ") + error.what());
    } catch (const std::exception& error) {
        return Error(std::string("Unexpected indexing error: ") + error.what());
    }
}

Error Kernal::StartAll() {
    Log(GetName(), "Starting all subsystems...");

    Store* store = static_cast<Store*>(GetSubsystem(SubsystemId::Store));

    Error error = StartSubSystem_(SubsystemId::Store);
    if (HasError(error)) {
        return error;
    }

    if (store->HasSearchIndex()){
        Log(GetName(), "INDEX AVAILABLE");
    }else{

        for (SubsystemId id : {SubsystemId::DirReader, SubsystemId::Lexer, SubsystemId::Parser}) {
            error = StartSubSystem_(id);
            if (HasError(error)) {
                return error;
            }
        }

        error = AwaitIndexing_();
        if (HasError(error)) {
            return error;
        }
    }

    error = StartSubSystem_(SubsystemId::Engine);
    if (HasError(error)) {
        return error;
    }

    // for (const auto& name : order_) {
    //     Error error = subsystems_[name]->Start();

    //     if (HasError(error)) {
    //         std::cerr << "\n [" << GetName() << "] [" << name 
    //                   << "] Start failed, rolling back..." << std::endl;
            
    //         // ROLLBACK: Stop everything we already started, in reverse
    //         for (auto it = started.rbegin(); it != started.rend(); ++it) {
    //             std::cerr << "\n [" << GetName() << "] Rolling back [" << *it << "]" << std::endl;
    //             subsystems_[*it]->Stop();
    //         }
            
    //         return error;
    //     }

    //     started.push_back(name);
    // }

    return Error("");
}

Error Kernal::StopAll() {
    Log(GetName(), "Stopping all subsystems...");

    std::string errors;

    // Reverse order: stop in reverse registration
    for (auto it = order_.rbegin(); it != order_.rend(); ++it) {
        const std::string name = ToStdString(*it);
        Subsystem* subsystem = subsystems_[*it].get();
        Error error = subsystem->Stop();

        if (HasError(error)) {
            Log(GetName(), "[" + name + "] Stop failed: " + error.GetMessage());
            errors += "[" + name + "] Stop failed: " + error.GetMessage() + "\n";
        } else {
            Log(GetName(), "[" + name + "] Stop successful");
        }
    }

    if (!errors.empty()) {
        return Error("Errors occurred while stopping subsystems:\n" + errors);
    }

    return Error("");
}

Subsystem* Kernal::GetSubsystem(SubsystemId id) const {
    auto it = subsystems_.find(id);
    if (it != subsystems_.end()) {
        return it->second.get();
    }
    return nullptr;
}

Error Kernal::StartSubSystem_(SubsystemId id){
    Error error = subsystems_[id]->Start();
    return error;
}