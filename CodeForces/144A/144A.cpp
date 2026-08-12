#include <iostream>
#include <vector>
#include <algorithm>

int main() {

    int n;
    std::cin >> n;

    std::vector<int> weights(n);

    for (int& x : weights) {
        std::cin >> x;
    }

    // First occurrence of maximum
    auto max_it = std::max_element(weights.begin(), weights.end());
    int maxIndex = std::distance(weights.begin(), max_it);

    // Last occurrence of minimum
    auto min_it = std::min_element(weights.rbegin(), weights.rend());
    int minIndex = n - 1 - std::distance(weights.rbegin(), min_it);

    int swaps = maxIndex + (n - 1 - minIndex);

    if (maxIndex > minIndex) {
        swaps--;
    }

    std::cout << swaps << '\n';

    return 0;
}