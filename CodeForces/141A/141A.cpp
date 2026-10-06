#include <iostream>
#include <string>
#include <unordered_map>


int main() {

    std::string word1;
    std::string word2;
    std::string pile;

    std::cin >> word1 >> word2 >> pile;

    if ((word1.size() + word2.size()) != pile.size()){
        std::cout << "NO" << std::endl;
        return 0;
    };

    std::unordered_map<char, int> charMap;

    for (size_t i = 0; i < word1.size(); ++i){
        auto result = charMap.try_emplace(word1[i], 1);

        if (!result.second){
            ++(result.first->second);
        };
    }

    for (size_t i = 0; i < word2.size(); ++i){
        auto result = charMap.try_emplace(word2[i], 1);

        if (!result.second){
            ++(result.first->second);
        };
    }


    for (size_t i = 0; i < pile.size(); ++i){
        auto it = charMap.find(pile[i]);

        if (it != charMap.end()){
            --(it->second);
        };

    }


    for (const auto& [key, value] : charMap){
        if (value > 0){
            std::cout << "NO" << std::endl;
            return 0;
        };
    }

    std::cout << "YES" << std::endl;



    return 0;
}