#pragma once
#include "internal/kernal/core/headerfiles/error.h"
#include "internal/kernal/core/headerfiles/subsystem.h"
#include "internal/kernal/core/headerfiles/token.h"
#include <cstdio>
#include <cstring>
#include <string>

class Lexer : public Subsystem {

    public:
        Lexer();
        

        std::string Name() override;
        Error Init() override;
        Error Start() override;
        Error Stop() override;
        Error Run();   
        void SetFile(const std::string& filename);

    private:
        FILE* file;
        char line[456];
        size_t line_count = 0;
        std::string filepath;


};