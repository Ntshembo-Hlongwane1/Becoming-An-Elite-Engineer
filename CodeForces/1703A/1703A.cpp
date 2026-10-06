#include <iostream>
#include <string>

int main(){

    int testCount;
    std::cin >> testCount;

    for (int i = 0; i < testCount; ++i){
        std::string yes;
        std::cin >> yes;

        bool firstLetterCompare = std::tolower(static_cast<unsigned char>(yes[0])) == 'y';
        bool secondLetterCompare = std::tolower(static_cast<unsigned char>(yes[1])) == 'e';
        bool thirdLetterCompare = std::tolower(static_cast<unsigned char>(yes[2])) == 's';

        if (firstLetterCompare && secondLetterCompare && thirdLetterCompare){
            std::cout << "YES" << std::endl;
        }else{
            std::cout << "NO" << std::endl;
        };
    }

    return 0;
}