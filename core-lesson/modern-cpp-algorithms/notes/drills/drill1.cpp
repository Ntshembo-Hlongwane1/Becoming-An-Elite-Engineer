#include <iostream>
#include <vector>
#include "include/file.hpp"
#include <string>

int main(){

    std::cout << "DRILL 1" << std::endl;

    std::vector<char> buffer(4096, 'x'); 

    std::string path = "path.txt";
    FileManager fm = FileManager{ path };

    // Sequential Write
    for (int i = 0; i < 16384; ++i){
        fm.Write(buffer);
    }
    std::cout << "\n";
    // Random Write
    for (int i = 0; i < 16384; ++i){
        fm.WriteAt(buffer, i * 5);
    };
    return 0;
}