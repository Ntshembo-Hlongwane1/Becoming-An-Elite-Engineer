#include <iostream>

int main(){

    int input;
    std::cin >> input;

    int maxCount = 0;
    int currentPassengers = 0;

    for (int i = 0; i < input; i++){
        int currentExit, currentEntry;
        std::cin >> currentExit >> currentEntry;

        currentPassengers = currentPassengers - currentExit + currentEntry;

        if (currentPassengers > maxCount){
            maxCount = currentPassengers;
        }
    }

    std::cout << maxCount << std::endl;
    return 0;
}
