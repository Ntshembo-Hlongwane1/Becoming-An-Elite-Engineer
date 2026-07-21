#include "lexer.hpp"
#include "internal/kernal/core/headerfiles/error.hpp"
#include <iostream>

Lexer::Lexer() : file(nullptr), line_count(0){
    line[0] ='\0';
};

std::string Lexer::Name() {
    return "Lexer";
};

Error Lexer::Init(){
    std::cout << "["<< Name() << "] Initializing.." << std::endl;
    return Error("");
};

Error Lexer::Start(){
    std::cout << "[" << Name() << "] Starting..." << std::endl;
    return Error("");
};

Error Lexer::Stop(){
    std::cout << "[" << Name() <<"] Stopping..." << std::endl;
    return Error("");
};

Error Lexer::Run(){
    std::cout << "[" << Name() << "] Run starting..." << std::endl;

    return Error("");
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

