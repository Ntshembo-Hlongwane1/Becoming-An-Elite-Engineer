#include <iostream>
#include <vector>

int main(){

    int n, k;

    std::cin >> n >> k;

    std::vector<int> scores(n);

    for (int i = 0; i < n; i++){
        int grade;
        std::cin >> grade;
        scores[i] = grade;
    };

    int baseVal = scores[k - 1];

    int result = 0;

    for (std::size_t i = 0; i < scores.size(); i++){
        if (scores[i] >= baseVal && scores[i] > 0){
            result++;
        }
    }

    std::cout << result << std::endl;

    return 0;
}