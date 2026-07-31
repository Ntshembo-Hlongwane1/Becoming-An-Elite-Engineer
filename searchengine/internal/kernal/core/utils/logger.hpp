#pragma once  
#include <iostream>
#include <string>
#include <string_view>

inline void Log(std::string_view name, std::string_view message) {
    std::cout << "\n[" << name << "] " << message << std::endl;
}