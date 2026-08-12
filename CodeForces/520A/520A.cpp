#include <iostream>
#include <string>

int main() {
    int n;
    std::string word;

    std::cin >> n >> word;

    if (n < 26) {
        std::cout << "NO\n";
        return 0;
    }

    bool seen[26] = {};

    for (char c : word) {
        if (c >= 'A' && c <= 'Z') {
            c = c - 'A' + 'a';
        }

        seen[c - 'a'] = true;
    }

    for (int i = 0; i < 26; i++) {
        if (!seen[i]) {
            std::cout << "NO\n";
            return 0;
        }
    }

    std::cout << "YES\n";
}