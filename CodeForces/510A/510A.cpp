#include <iostream>

void printFull(int count){
    for (int i = 0; i < count; i++){
        std::cout << "#";
    };
    std::cout << std::endl;
}

void printFromLeft(int count){
    std::cout << "#";

    for (int i = 0; i < count - 1; i++){
        std::cout << ".";
    };
    std::cout << std::endl;
}

void printFromRight(int count){
    for (int i = 0; i < count - 1; i++){
        std::cout << ".";
    };
    std::cout << "#" << std::endl;
}

int main(){
    
    int n, m;
    std::cin >> n >> m;

    for (int i = 0; i < n; i++){
        if((i + 1) % 2 != 0){
            printFull(m);
        }else{
            if ((i + 1) % 4 == 2){
                printFromRight(m);
            }else{
                printFromLeft(m);
            }
        }
    }
}