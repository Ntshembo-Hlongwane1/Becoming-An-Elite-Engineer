#include "lexer.hpp"
#include "internal/kernal/core/headerfiles/error.hpp"
#include <iostream>
#include <string_view>
#include "ilp.hpp"
#include <thread>



std::string Lexer::Name() {
    return "Lexer";
};

Error Lexer::Init(){
    std::cout << "["<< Name() << "] Initializing.." << std::endl;
    return Error("");
};

Error Lexer::Start(){
    std::cout << "\n [" << Name() << "] Starting..." << std::endl;

    std::thread worker1;
    std::thread worker2;

    return Error("");
};

Error Lexer::Stop(){
    std::cout << "\n [" << Name() <<"] Stopping..." << std::endl;

   
    return Error("");
};

void Lexer::Run(){
    std::cout << "[" << Name() << "] Run starting..." << std::endl;

    int dataFilesReceived = 0;

    while (true){
        std::string file;

        if (dirQueue.pop(file)){
            if (file == ""){
                std::cout << "\n [" << Name() << "] POISON PILL. Exiting" << std::endl;
                break;
            }

            dataFilesReceived++;

            if (file.empty()){
                break;
            }

            FILE* fp = fopen(file.c_str(), "r");

            if (!fp){
                std::cout << "\n [" << Name() << "] Failed to open data file" << std::endl;
            }

            char line[456];

            while(fgets(line, sizeof(line), fp) != NULL){
                size_t len = strlen(line);

                if (len > 0 && line[len - 1] == '\n'){
                    line[len - 1] = '\0';
                }

                std::cout << line << std::endl;
            }

            std::cout << "\n [" << Name() << "] Received: " << file << std::endl;
            
        }
    }

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

