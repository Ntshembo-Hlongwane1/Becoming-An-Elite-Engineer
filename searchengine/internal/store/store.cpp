#include "store.h"
#include <iostream>

Store::Store(){}

std::string Store::Name() {
    return "Store";
};

Error Store::Init() {
    std::cout << "\n [" << Name() << "] Initializing..." << std::endl;
    return Error("");
};

Error Store::Start() {
    std::cout << "\n [" << Name() << "] Starting..." << std::endl;
    return Error("");
};

Error Store::Stop() {
    std::cout << "\n [" << Name() << "] Stopping..." << std::endl;
    return Error("");
};

void Store::AddDataFile(const std::string& filePath) {
    dataFiles_.push_back(filePath);
};

std::vector<std::string> Store::GetDataFiles() const {
    return dataFiles_;
};

void Store::AddSearchIndex(std::string key, const std::string& doc){
    searchIndex_[key].push_back(doc);
};

const std::unordered_map<std::string, std::vector<std::string>>& Store::GetSearchIndex() const {
    return searchIndex_;
};