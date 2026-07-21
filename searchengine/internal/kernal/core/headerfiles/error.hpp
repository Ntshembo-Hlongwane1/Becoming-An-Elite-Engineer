#pragma once
#include <string>

class Error{
    public:
        Error(const std::string& message);
        void print() const;
        std::string GetMessage() const;

    private:
        std::string message;
};