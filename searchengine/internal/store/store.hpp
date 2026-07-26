#pragma once
#include "internal/kernal/core/headerfiles/subsystem.hpp"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

class Store : public Subsystem {
public:
    Store() = default;
    std::string Name() override;
    void AddDataFile(const std::string& filePath);
    std::vector<std::string> GetDataFiles() const;
    void AddSearchIndex(std::string key, const std::string& doc);
    const std::unordered_map<std::string, std::vector<std::string>>& GetSearchIndex() const;

protected:
    Error OnInit() override;
    Error OnStart() override;
    Error OnStop() override;

private:
    std::vector<std::string> dataFiles_;
    std::unordered_map<std::string, std::vector<std::string>> searchIndex_;
};