#include "lexer.hpp"
#include "internal/kernal/core/headerfiles/error.hpp"
#include <iostream>
#include <string_view>
#include "ilp.hpp"
#include <thread>
#include "internal/kernal/core/utils/stopwords.hpp"
#include <unordered_set>
#include <algorithm>
#include <cctype>
#include "internal/kernal/core/headerfiles/sharedmutexs.hpp"

 
std::string Lexer::Name() {
    return "Lexer";
};

Error Lexer::Init(){
    std::cout << "["<< Name() << "] Initializing.." << std::endl;
    return Error("");
};

Error Lexer::Start(){
    std::cout << "\n [" << Name() << "] Starting..." << std::endl;
    try{
        worker1 = std::thread(&Lexer::worker, this, "1"); 
        worker2 = std::thread(&Lexer::worker, this, "2"); 
    }catch(const std::system_error& e){
        std::cout << "Thread creation failed: " << std::string(e.what()) << std::endl;
        return Error("Thread creation failed" + std::string(e.what()));
    }    
    return Error("");
};

Error Lexer::Stop(){
    std::cout << "\n [" << Name() <<"] Stopping..." << std::endl;

   
    return Error("");
};


std::vector<std::string> Lexer::splitLine(std::string& line) {
    std::vector<std::string> result;
    std::size_t start = 0;
    std::size_t end = line.find(' ');

    while (end != std::string::npos) {
        result.push_back(line.substr(start, end - start));
        start = end + 1;
        end = line.find(' ', start);
    }
    result.push_back(line.substr(start));  // last token
    return result;
}

void Lexer::worker(std::string thread){
    std::cout << "WORKER STARTING: " << thread <<  std::endl;

    while(running.load(std::memory_order_acquire)){
        ILP line;

        lineQueue.pop_blocking(line);

       if (line.filepath.empty() && line.line.empty()) {
            std::cout << "Thread " << thread << " exiting." << std::endl;
            break;
        }

        auto tokens = splitLine(line.line);
        for (auto& token : tokens) {
            token.erase(
                std::remove_if(token.begin(), token.end(),
                    [](unsigned char c) {
                        return std::isspace(c) || std::ispunct(c);
                    }
                ),
                token.end()
            );

            if (StopWords::isStopWord(token) && !token.empty()){
                parserQueue.push_blocking(token);
                std::cout << "Token pushed: " << std::endl;
            }
        }
        
    }

}

void Lexer::Run(){
    std::cout << "[" << Name() << "] Run starting..." << std::endl;

    int dataFilesReceived = 0;

    while (true){
        std::string file;

        dirQueue.pop_blocking(file);

        if (file.empty()){
            std::cout << "\n [" << Name() << "] POISON PILL. Exiting" << std::endl;
            break;
        }

        dataFilesReceived++;

        FILE* fp = fopen(file.c_str(), "r");

        if (!fp){
            std::cout << "\n [" << Name() << "] Failed to open data file" << std::endl;
            continue;
        }

        char line[456];

        while(fgets(line, sizeof(line), fp) != nullptr){
            size_t len = strlen(line);

            if (len > 0 && line[len - 1] == '\n'){
                line[len - 1] = '\0';
            }

            lineQueue.push_blocking({ file, std::string(line) }); 

        }

        if (ferror(fp)) {
            std::cerr << "[" << Name() << "] Read error on file: " << file << std::endl;
        }

        fclose(fp);
    }

    std::cout << "Data Files: " << dataFilesReceived << std::endl;
    //running.store(false, std::memory_order_release);

};


void Lexer::SetFile(const std::string& filename){
    if (file && file != stdin){
        fclose(file);
        file = nullptr;
    };

    filepath = filename;

    line_count = 0;
    line[0] = '\0';

};

