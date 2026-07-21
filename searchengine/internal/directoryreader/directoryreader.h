#pragma once
#include "internal/kernal/core/headerfiles/subsystem.h"
#include "internal/store/store.h"

class DirectoryReader : public Subsystem{
    public:
        std::string Name() override;
        Error Init() override;
        Error Start() override;
        Error Stop() override;

    private:
        Store* store_;
        std::string getRightPart(std::string str);
};