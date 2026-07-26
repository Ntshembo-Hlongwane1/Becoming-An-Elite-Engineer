#include "kernal.hpp"
#include <iostream>
 
Kernal::~Kernal(){
    for (auto it = order_.rbegin(); it != order_.rend(); ++it) {
        Subsystem* sub = subsystems_[*it].get();
        if (sub->GetState() == Subsystem::State::STARTED) {
            std::cerr << "\n [" << GetName() << "] WARNING: " << *it 
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

Error Kernal::Register(const std::string& name, Subsystem* subsystem) {
    if (subsystems_.find(name) != subsystems_.end()) {
        delete subsystem; // avoid leak
        return Error("[" + name + "] Subsystem already registered");
    }

    subsystems_[name] = std::unique_ptr<Subsystem>(subsystem);
    order_.push_back(name);

    std::cout << "\n [" << GetName() << "] [" << name << "] Subsystem registered" << std::endl;
    return Error("");
}

Error Kernal::InitAll() {
    std::cout << "\n [" << GetName() << "] Initializing all subsystems..." << std::endl;

    for (const auto& name : order_) {
        Subsystem* subsystem = subsystems_[name].get();
        Error error = subsystem->Init();

        if (HasError(error)) {
            std::cout << "\n [" << GetName() << "] [" << name << "] Initialization failed: "
                      << error.GetMessage() << std::endl;
            return Error("[" + name + "] Initialization failed: " + error.GetMessage());
        }

        std::cout << "\n [" << GetName() << "] [" << name << "] Initialization successful" << std::endl;
    }

    std::cout << "\n [" << GetName() << "] All subsystems initialized successfully" << std::endl;
    return Error("");
}

Error Kernal::StartAll() {
    std::cout << "\n [" << GetName() << "] Starting all subsystems..." << std::endl;

    std::vector<std::string> started;

    for (const auto& name : order_) {
        Error error = subsystems_[name]->Start();

        if (HasError(error)) {
            std::cerr << "\n [" << GetName() << "] [" << name 
                      << "] Start failed, rolling back..." << std::endl;
            
            // ROLLBACK: Stop everything we already started, in reverse
            for (auto it = started.rbegin(); it != started.rend(); ++it) {
                std::cerr << "\n [" << GetName() << "] Rolling back [" << *it << "]" << std::endl;
                subsystems_[*it]->Stop();
            }
            
            return error;
        }

        started.push_back(name);
    }

    std::cout << "\n [" << GetName() << "] All subsystems started" << std::endl;
    return Error("");
}

Error Kernal::StopAll() {
    std::cout << "\n [" << GetName() << "] Stopping all subsystems..." << std::endl;

    std::string errors;

    // Reverse order: stop in reverse registration
    for (auto it = order_.rbegin(); it != order_.rend(); ++it) {
        const std::string& name = *it;
        Subsystem* subsystem = subsystems_[name].get();
        Error error = subsystem->Stop();

        if (HasError(error)) {
            std::cout << "\n [" << GetName() << "] [" << name << "] Stop failed: "
                      << error.GetMessage() << std::endl;
            errors += "[" + name + "] Stop failed: " + error.GetMessage() + "\n";
        } else {
            std::cout << "\n [" << GetName() << "] [" << name << "] Stop successful" << std::endl;
        }
    }

    if (!errors.empty()) {
        return Error("Errors occurred while stopping subsystems:\n" + errors);
    }

    return Error("");
}

Subsystem* Kernal::GetSubsystem(const std::string& name) const {
    auto it = subsystems_.find(name);
    if (it != subsystems_.end()) {
        return it->second.get();
    }
    return nullptr;
}