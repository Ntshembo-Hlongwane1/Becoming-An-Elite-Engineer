#include <string>
#include "headerfiles/error.hpp"
#include <iostream>
#include "internal/kernal/core/utils/logger.hpp"

Error::Error(const std::string& message) : message(message) {}

void Error::print() const {
    Log("Error", message);
}

std::string Error::GetMessage() const {
    return message;
}