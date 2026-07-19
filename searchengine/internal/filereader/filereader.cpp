#include "filereader.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <unordered_set>

const std::unordered_set<std::string> stopWords = {
    "a", "an", "the", "and", "or", "but", "for", "nor", "on", "at", 
    "to", "by", "in", "of", "is", "it", "are", "was", "were", "been",
    "be", "that", "which", "who", "whom", "whose", "have", "has", 
    "had", "do", "does", "did", "will", "would", "could", "should",
    // Add as many as you need. 100-300 is typical for NLP.
};

FileReader::FileReader(Store* store) : store_(store) {}

std::string FileReader::Name() {
    return "File Reader";
}

Error FileReader::Init() {
    std::cout << "\n [" << Name() << "] Initializing..." << std::endl;
    return Error("");
}

Error FileReader::Start() {
    std::cout << "\n [" << Name() << "] Starting..." << std::endl;
    
    std::string dataPath = "data";

    // If "data" is not found in CWD, check parent directory (useful if running from build/)
    if (!std::filesystem::exists(dataPath) || !std::filesystem::is_directory(dataPath)) {
        dataPath = "../data";
    }

    if (!std::filesystem::exists(dataPath) || !std::filesystem::is_directory(dataPath)) {
        return Error("Data directory not found at 'data' or '../data'");
    }

    for (const auto& entry : std::filesystem::directory_iterator(dataPath)){
        if (entry.is_regular_file()){
            store_->AddDataFile(getRightPart(entry.path().string()));

            std::ifstream file(entry.path());

            if (!file.is_open()){
                continue;
            };

            std::string line;

            while(std::getline(file, line)){
                std::string_view view = line;

                size_t start = 0;
                size_t end = view.find(' ');

                while (end != std::string_view::npos){
                    std::string_view word = view.substr(start, end - start);

                    // MOVE POINTERS FIRST so 'continue' doesn't cause an infinite loop
                    start = end + 1;
                    end = view.find(' ', start);

                    // HANDLE THE WORD CHECK IF STOP WORD OR TOKENIZE
                    std::string wordStr(word);

                    if (stopWords.find(wordStr) != stopWords.end()){
                        continue;
                    }else{
                        store_->AddSearchIndex(wordStr, entry.path().string());
                    };
                };

                
                // Handling the last word after the final space

                std::string_view lastWord = view.substr(start);

                if (!lastWord.empty()){
                     if (stopWords.find(std::string(lastWord)) != stopWords.end()){
                        continue;
                    }else{
                        store_->AddSearchIndex(std::string(lastWord), entry.path().string());
                    };
                    
                }
            }
        }
    }
    
    std::cout << "\n [" << Name() << "] Data files added to store." << std::endl;

    return Error("");
}

Error FileReader::Stop() {
    std::cout << "\n [" << Name() << "] Stopping..." << std::endl;
    return Error("");
};

std::string FileReader::getRightPart(std::string str){
    size_t pos = str.find('\\');

    if (pos == std::string::npos){
        return str;
    };

    return str.substr(pos + 1);
};