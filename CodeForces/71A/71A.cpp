#include <iostream>
#include <string>
#include <vector>

int main(){

    int n;

    std::cin >> n;

    std::vector<std::string> data;

    for (int i = 0; i < n; i++){
        std::string word;
        std::cin >> word;

        data.emplace_back(word);
    }

    for (size_t i = 0; i < data.size(); i++){
        if (data[i].length() > 10){
            std::cout << data[i][0] << data[i].length() - 2 << data[i][data[i].length() -1] << std::endl;
        }else{
            std::cout << data[i] << std::endl;
        }
    }

    return 0;
};

