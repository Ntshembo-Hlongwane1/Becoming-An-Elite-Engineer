#include <iostream>
#include <string>

int main(){

    int n;
    std::cin >> n;

    std::string input;
    std::cin >> input;

    int aCount = 0;
    int dCount = 0;

    for (int i = 0; i < n; i++){
        if (std::tolower(static_cast<unsigned char>(input[i])) == 'a'){
            aCount++;
        }else if (std::tolower(static_cast<unsigned char>(input[i])) == 'd'){
            dCount++;
        }
    }


    if (aCount == dCount){
        std::cout << "Friendship" << std::endl;
    }else if (aCount > dCount){
        std::cout << "Anton" << std::endl;
    }else{
        std::cout << "Danik" << std::endl;
    };

    return 0;
}