#include <iostream>
#include <vector>

int main(){

    int cardCount;
    std::cin >> cardCount;

    std::vector<int> cards;

    for (int i = 0; i < cardCount; ++i){
        int card;
        std::cin >> card;
        cards.emplace_back(card);
    }

    size_t leftPtr = 0;
    size_t rightPtr = cards.size() - 1;

    int serejaPoints = 0;
    int dimaPoints = 0;


    for (size_t i = 0; i < cards.size(); ++i){
        if (((i + 1) % 2) == 1){
            if (cards[leftPtr] > cards[rightPtr]){
                serejaPoints += cards[leftPtr];
                ++leftPtr;
            }else{
                serejaPoints += cards[rightPtr];
                --rightPtr;
            };
        }else{
            if (cards[leftPtr] > cards[rightPtr]){
                dimaPoints += cards[leftPtr];
                ++leftPtr;
            }else{
                dimaPoints += cards[rightPtr];
                --rightPtr;
            };
        }
    }


    std::cout << serejaPoints << " " << dimaPoints << std::endl;
    
    return 0;
}