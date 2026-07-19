#include <iostream>


bool atLeastTwoAreYes(int a, int b, int c){
    return (a + b + c) >= 2;
}

int main() {

    int qnCount;
    std::cin >> qnCount;

    int questionsCanResolveCount = 0;

    for (int i = 0; i < qnCount; i++){
        int a, b, c;

        std::cin >> a >> b >> c;

        if (atLeastTwoAreYes(a, b, c)){
            questionsCanResolveCount++;
        }
    }
    
    std::cout << questionsCanResolveCount << std::endl;
    return 0;
}