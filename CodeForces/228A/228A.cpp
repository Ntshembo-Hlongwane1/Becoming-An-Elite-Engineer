#include <iostream>
#include <vector>
#include <algorithm>

int main(){

    int buyCount = 0;
    std::vector<int> seenMap(4);


    for (int i = 0; i < 4; i++){
        int n;
        std::cin >> n;

        auto it = std::find(seenMap.begin(), seenMap.end(), n);

        if (it != seenMap.end()){
            buyCount++;
        }else{
            seenMap[i] = n;
        }
    }

    std::cout << buyCount << std::endl;

    return 0;
}