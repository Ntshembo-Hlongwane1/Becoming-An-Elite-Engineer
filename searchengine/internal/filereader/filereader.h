#pragma once
#include "internal/kernal/core/headerfiles/subsystem.h"
#include "internal/store/store.h"

class FileReader : public Subsystem{
    public:
        FileReader(Store* store);

        std::string Name() override;
        Error Init() override;
        Error Start() override;
        Error Stop() override;

    private:
        Store* store_;
        std::string getRightPart(std::string str);
};