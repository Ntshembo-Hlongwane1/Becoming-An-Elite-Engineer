#include <iostream>
#include <vector>
#include <array>


void solve(const std::vector<int>& array){

    std::array<int, 101> count{};
    for (int value : array){
        count[value]++;
    }

    size_t printed = 0;
    while (printed < array.size()){
        for (int value = 100; value >= 1; --value){
            if (count[value] > 0){
                std::cout << value << " ";
                count[value]--;
                printed++;
            }
        }
    }
}

int main() {

    int testCount;
    int arraySize;
    std::vector<int> array;

    std::cin >> testCount;

    for (int i = 0; i < testCount; ++i){
        std::cin >> arraySize;
        array.resize(arraySize);

        for (int j = 0; j < arraySize; ++j){
            std::cin >> array[j];
        }

        solve(array);
        std::cout << "\n";
    }

    return 0;
}
