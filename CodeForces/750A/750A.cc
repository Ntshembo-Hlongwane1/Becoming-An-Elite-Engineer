#include <iostream>
#include <chrono>

int main(){
    int problemsToSolveCount;
    int distanceToEvent;

    std::cin >> problemsToSolveCount >> distanceToEvent;

    int contestDuration = 240;
    int availableTime = contestDuration - distanceToEvent; 
    
    int problemsSolved = 0;
    for (int i = 0; i < problemsToSolveCount; ++i){
        int timeRequired = (i + 1) * 5;

        if (timeRequired <= availableTime){
            ++problemsSolved;
            availableTime -= timeRequired;
        };
    };

    std::cout << problemsSolved << std::endl;

    return 0;
}