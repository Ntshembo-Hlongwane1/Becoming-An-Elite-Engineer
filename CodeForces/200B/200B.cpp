#include <iostream>

int main(){

    int n;
    std::cin >> n;

    double total = 0;

    for (int i = 0; i < n; i++){
        double juice;
        std::cin >> juice;
        total += juice;
    }

    std::cout << (total / n) << std::endl;

    return 0;
}