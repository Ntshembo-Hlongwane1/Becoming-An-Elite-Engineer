#include <iostream>
#include <unordered_map>

int main(){

    std::unordered_map<int, int> reverseLookUp;

    int n;
    std::cin >> n;

    for (int i = 0; i < n; i++){
        int giftReceiver;
        std::cin >> giftReceiver;
        reverseLookUp[giftReceiver] = i + 1;
    }

    for (int i = 0; i < n; i++){
        std::cout << reverseLookUp[i + 1] << ' ';
    };

    std::cout << '\n';

    return 0;
}