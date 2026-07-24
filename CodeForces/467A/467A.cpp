#include <iostream>

int main(){

    int rooms;
    std::cin >> rooms;

    int availableRooms = 0;
    for (int i = 0; i < rooms; i++){
        int currenOccupancy, maxCapacity;

        std::cin >> currenOccupancy >> maxCapacity;

        if ((maxCapacity - currenOccupancy) >= 2){
            availableRooms++;
        }
    }

    std::cout << availableRooms << std::endl;

    return 0;
}