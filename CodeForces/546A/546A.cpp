#include <iostream>
#include <cstdlib>

int main(){

    int k, n, w;

    std::cin >> k >> n >> w;


    for (int i = 0; i < w; i++){
        n -= (i + 1) * k;
    }

    if (n < 0){
        std::cout << abs(n) << std::endl;
    }else{
        std::cout << 0 << std::endl;
    }

    return 0;
}