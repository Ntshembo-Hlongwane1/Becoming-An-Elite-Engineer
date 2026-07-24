#include "directoryreader.hpp"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <unordered_set>

std::string DirectoryReader::Name() {
    return "Dir Reader";
}

Error DirectoryReader::Init() {
    std::cout << "\n [" << Name() << "] Initializing..." << std::endl;
    return Error("");
}

Error DirectoryReader::Start() {
    std::cout << "\n [" << Name() << "] Starting..." << std::endl;
    return Error("");
}

void DirectoryReader::Run() {
    std::cout << "\n [" << Name() << "] Starting..." << std::endl;
    
    std::string dataPath = "data";


    // if (!std::filesystem::exists(dataPath) || !std::filesystem::is_directory(dataPath)) {
    //     return Error("Data directory not found at 'data' or '../data'");
    // }

    for (const auto& entry : std::filesystem::directory_iterator(dataPath)){
        if (entry.is_regular_file()){
            dirQueue.push(entry.path().string());
        }
    }
    
    std::cout << "\n [" << Name() << "] Data files added to store." << std::endl;

}

Error DirectoryReader::Stop() {
    std::cout << "\n [" << Name() << "] Stopping..." << std::endl;
    return Error("");
};

std::string DirectoryReader::getRightPart(std::string str){
    size_t pos = str.find('\\');

    if (pos == std::string::npos){
        return str;
    };

    return str.substr(pos + 1);
};