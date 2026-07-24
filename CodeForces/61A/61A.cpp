#include <iostream>
#include <string>

int main(){

    std::string num1;
    std::string num2;
    std::cin >> num1 >> num2;

    for (size_t i = 0; i < num1.length(); i++){
        if (num1[i] == num2[i]){
            std::cout << "0";
        }else{
            std::cout << "1";
        }
    }

    std::cout << '\n';

    return 0;
}