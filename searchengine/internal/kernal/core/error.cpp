#include <string>
#include "headerfiles/error.hpp"
#include <iostream>

Error::Error(const std::string& message) : message(message) {}

void Error::print() const {
    std::cout << "Error: " << message << std::endl;
}

std::string Error::GetMessage() const {
    return message;
}