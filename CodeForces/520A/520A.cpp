#include <iostream>

int main(){
    std::string letters = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

    std::cout << "LEtter size" << (letters.size()) << std::endl;
    int n;
    std::string word;
    std::cin >> n;
    std::cin >> word;


    if (n < 26){
        std::cout << "NO" << std::endl;
        return 0;
    }

    for (size_t i = 0; i < word.size(); i++){
        if (letters.find(word[i]) != std::string::npos){
            std::cout << "Condition hit: " << std::endl;
            letters.erase(i, 1);
        }
    }

    std::cout << "Remaining: " << (letters.size()) << std::endl;
    if (letters.size() <= 26){
        std::cout << "YES" << std::endl;
    }else{
        std::cout << "NO" << std::endl;
    }

    return 0;
}