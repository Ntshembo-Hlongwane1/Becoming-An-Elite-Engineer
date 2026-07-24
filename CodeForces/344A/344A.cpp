#include <iostream>
#include <utility>
#include <vector>

int main(){

    int n;
    std::cin >> n;
    std::vector<std::pair<int, int>> magnets;

    for (int i = 0; i < n; i++){
        int leftCharge, rightCharge;
        std::cin >> leftCharge >> rightCharge;
        magnets.emplace_back(leftCharge, rightCharge);
    }

    int attractionCount = 1;
    for (int i = 1; i < n; i += 1){
        if (magnets[i - 1].second == magnets[i].first){
            attractionCount++;
        }
    }

    std::cout << attractionCount << std::endl;

    return 0;
}

/*\

    6
10X10X10 01 10X10

  */