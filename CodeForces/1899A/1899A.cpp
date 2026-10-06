#include <iostream>

int main(){

    int testCount;
    std::cin >> testCount;

    for (int i = 0; i < testCount; ++i){
        int n;
        std::cin >> n;

        if (n % 3 == 0){
            std::cout << "Second" << std::endl;
        }else{
            std::cout << "First" << std::endl;
        };
    };

    return 0;
}