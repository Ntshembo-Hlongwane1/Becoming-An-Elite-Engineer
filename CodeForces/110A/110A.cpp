#include <iostream>

int main(){

    long long input;
    std::cin >> input;

    int foundLuckyCount = 0;
    
    while (input > 0){
        if (input % 10 == 4 || input % 10 == 7){
            foundLuckyCount++;
        }

        input /= 10;
    }


    std::cout << (foundLuckyCount == 4 || foundLuckyCount == 7 ? "YES" : "NO") << '\n';
    return 0;
}