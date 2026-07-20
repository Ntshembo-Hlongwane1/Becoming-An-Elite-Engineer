#include <iostream>

int main(){

    int n, h;

    std::cin >> n >> h;

    int minWidthForRoad = 0;

    for (int i = 0; i < n; i++){
        int input;
        std::cin >> input;

        if (input <= h){
            minWidthForRoad++;
        }else{
            minWidthForRoad +=  2;
        }
    }

    std::cout << minWidthForRoad << std::endl;

    return 0;
}