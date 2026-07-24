#include <iostream>

int main(){

    int n;
    std::cin >> n;

    bool isHard = false;
    for (int i = 0; i < n; i++){
        int vote;
        std::cin >> vote;

        if (vote == 1){
            isHard = true;
        };
    }

    std::cout << (isHard ? "HARD" : "EASY") << std::endl;

    return 0;
}