#include "store.hpp"
#include <iostream>

std::string Store::Name() { return "Store"; }

Error Store::OnInit() {
    return Error("");
}

Error Store::OnStart() {
    return Error("");
}

Error Store::OnStop() {
    return Error("");
}

void Store::AddDataFile(const std::string& filePath) {
    dataFiles_.push_back(filePath);
}

std::vector<std::string> Store::GetDataFiles() const {
    return dataFiles_;
}

void Store::AddSearchIndex(std::string key, const std::string& doc) {
    searchIndex_[key].push_back(doc);
}

const std::unordered_map<std::string, std::vector<std::string>>& Store::GetSearchIndex() const {
    return searchIndex_;
}