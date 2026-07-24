#include <iostream>
#include <vector>
#include <tuple>

void getAllPairs(std::vector<char> &queue, std::vector<int> &swapPositions){

    for (size_t i = 0; i + 1 < queue.size(); i++){
        if (queue[i] == 'B' && queue[i + 1] == 'G'){
            swapPositions.emplace_back(i);
        };
    };

}

void performSwap(std::vector<char> &queue, std::vector<int> &swapPositions){
    for (size_t i = 0; i < swapPositions.size(); i++){
        char temp = queue[swapPositions[i]];
        queue[swapPositions[i]] = queue[swapPositions[i] + 1];  
        queue[swapPositions[i] + 1] = temp;
    }
}

void printQueue(std::vector<char> &queue){
    for (size_t i = 0; i < queue.size(); i++){
        std::cout << queue[i];
    };
    std::cout << '\n';
}

int main(){
    int n, t;
    std::cin >> n >> t;

    std::vector<char> queue(n);


    for (int i = 0; i < n; i++){
        char gender;
        std::cin >> gender;
        queue[i] = gender;
    }


    std::vector<int> swapPositions;

    for (int i = 0; i < t; i++){
        getAllPairs(queue, swapPositions);
        performSwap(queue, swapPositions);
        swapPositions.clear();
    }

    printQueue(queue);
    return 0;
}

