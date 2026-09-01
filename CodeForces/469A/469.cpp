#include <iostream>
#include <unordered_map>
 
int main(){
 
    std::unordered_map<int, bool> usedNumbers;
 
    int levels;
    int personsLevelsPassedCount;
 
    std::cin >> levels;
    std::cin >> personsLevelsPassedCount;
 
    usedNumbers.reserve(levels);
 
    // For personX
    for (int i = 0; i < personsLevelsPassedCount; i++){
        int personPassedLevel;
        std::cin >> personPassedLevel;
 
        auto it = usedNumbers.find(personPassedLevel);
 
        if (it == usedNumbers.end()){
            usedNumbers[personPassedLevel] = true;
        }
    }
 
    // For personY
    int yCount;
    std::cin >> yCount;
 
    for (int i = 0; i < yCount; i++){
        int personPassedLevel;
        std::cin >> personPassedLevel;
 
        auto it = usedNumbers.find(personPassedLevel);
 
        if (it == usedNumbers.end()){
            usedNumbers[personPassedLevel] = true;
        }
    }
 
    for (int i = 0; i < levels; i++){
        auto it = usedNumbers.find(i + 1);
 
        if (it == usedNumbers.end()){
            std::cout << "Oh, my keyboard!" << std::endl;
            return 0;
        }
    }
 
    std::cout << "I become the guy." << std::endl;
 
    return 0;
}