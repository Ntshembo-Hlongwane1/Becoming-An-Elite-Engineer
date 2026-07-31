#include "kernal.hpp"
#include <iostream>
#include "internal/store/store.hpp"
#include "internal/kernal/core/headerfiles/subsystem.hpp"
#include "internal/kernal/core/utils/logger.hpp"
 
Kernal::~Kernal(){
    for (auto it = order_.rbegin(); it != order_.rend(); ++it) {
        Subsystem* sub = subsystems_[*it].get();
        if (sub->GetState() == Subsystem::State::STARTED) {
            std::cerr << "\n [" << GetName() << "] WARNING: " << ToString(*it)
                      << " was not stopped before Kernal destruction. "
                      << "Stopping now." << std::endl;
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

Error Kernal::StartAll() {
    Log(GetName(), "Starting all subsystems...");

    std::vector<SubsystemId> started;

    Store* store = static_cast<Store*>(GetSubsystem(SubsystemId::Store));
    StartSubSystem_(SubsystemId::Store);

    if (store->HasSearchIndex()){
        Log(GetName(), "INDEX AVAILABLE");
    }else{
        
        StartSubSystem_(SubsystemId::DirReader);
        StartSubSystem_(SubsystemId::Lexer);
        StartSubSystem_(SubsystemId::Parser);

        Log(GetName(), "NO INDEX");
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