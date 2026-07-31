#include <iostream>
#include <unordered_map>

int main(){

    std::unordered_map<int, std::string> prefixWordMap = {
        {1, "I hate"},
        {2, "I love"}
    };
    
    std::unordered_map<int, std::string> suffixWordMap = {
        {1, "it"},
        {2, "that"}
    };

    int n; 
    std::cin >> n;

    for (int i = 0; i < n; i++){

        if ((i + 1) % 2 == 0){
            if (i + 1 >= n){
                std::cout << (prefixWordMap[2] + ' ' + suffixWordMap[1] + ' ') << std::endl;
            }else{
                std::cout << (prefixWordMap[2] + ' ' + suffixWordMap[2] + ' ') + ' ';
            };
        }else{
            if (i + 1 >= n){
                std::cout << (prefixWordMap[1] + ' ' + suffixWordMap[1] + ' ') << std::endl;
            }else{
                std::cout << (prefixWordMap[1] + ' ' + suffixWordMap[2]) + ' ';
            };
        }
    }

    std::cout << '\n';

    return 0;
}