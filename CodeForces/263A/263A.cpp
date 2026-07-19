#include <iostream>
#include <vector>

int main(){
    
    std::vector<std::vector<int>> matrix;

    for (int i = 0; i < 5; i++){
        int a, b, c, d, e;

        std::cin >> a >> b >> c >> d >> e;

        matrix.emplace_back(std::vector<int>{a, b, c, d, e});
    }

    for (size_t i = 0; i < matrix.size(); i++){
        for (size_t j = 0; j < matrix.size(); j++){
            if (matrix[i][j] == 1){
                std::cout << abs(i - 2) + abs(j - 2) << std::endl; 
                break;
            };
        };
    };



    return 0;
}