#include <iostream>
#include <algorithm>

int main(){

    int x1;
    int x2;
    int x3;

    std::cin >> x1 >> x2 >> x3;

    auto [small, large] = std::minmax({x1, x2, x3});

    std::cout << (large - small) << std::endl;

    return 0;

}