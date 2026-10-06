#include <iostream>

enum class Division {
    Division1 = 1900,
    Division2 = 1600,
    Division3 = 1400,
    Division4 = 0
};

constexpr int minRating(Division d) {
    return static_cast<int>(d);
}

int main() {
    int testCases;
    std::cin >> testCases;

    while (testCases--) {
        int rating;
        std::cin >> rating;

        if (rating >= minRating(Division::Division1)) {
            std::cout << "Division 1\n";
        } else if (rating >= minRating(Division::Division2)) {
            std::cout << "Division 2\n";
        } else if (rating >= minRating(Division::Division3)) {
            std::cout << "Division 3\n";
        } else {
            std::cout << "Division 4\n";
        }
    }

    return 0;
}