#include <iostream>
#include <string>

int main(){

    int upperCounter = 0;
    int lowerCounter = 0;

    std::string input;
    std::cin >> input;

    for (auto& ch : input){
        if (std::isupper(static_cast<unsigned char>(ch))){
            upperCounter++;
        }else{
            lowerCounter++;
        }
    }

    if (upperCounter > lowerCounter){
        for (auto it = input.begin(); it < input.end(); ++it){
            *it = std::toupper(static_cast<unsigned char>(*it));
        }

        std::cout << input << std::endl;
    }else{
        for (auto it = input.begin(); it < input.end(); ++it){
            *it = std::tolower(static_cast<unsigned char>(*it));
        }

        std::cout << input << std::endl;
    }


    return 0;
}