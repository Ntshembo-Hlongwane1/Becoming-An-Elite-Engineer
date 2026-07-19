#pragma once
#include "internal/kernal/core/headerfiles/subsystem.h"
#include <vector>
#include <string>
#include <unordered_map>

class Store : public Subsystem{
    public:
        Store();

        std::string Name() override;
        Error Init() override;
        Error Start() override;
        Error Stop() override;
        void AddDataFile(const std::string& filePath);
        std::vector<std::string> GetDataFiles() const;
        void AddSearchIndex(std::string key, const std::string& doc);
        const std::unordered_map<std::string, std::vector<std::string>>& GetSearchIndex() const;

        

    private:
        std::vector<std::string> dataFiles_;
        std::unordered_map<std::string, std::vector<std::string>> searchIndex_;
};