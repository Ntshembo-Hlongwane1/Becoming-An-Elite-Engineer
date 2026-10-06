#include <iostream>
#include <vector>

int main(){

    std::cout << "DRILL 1" << std::endl;

    std::vector<char> buffer(4096, 'x'); 

    for (int i = 0; i < 16384; ++i){
        // perform FILE I/O
    }
    return 0;
}