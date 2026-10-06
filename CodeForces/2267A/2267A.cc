#include <iostream>
#include <string>

bool isPalindrome(std::string& input){
    size_t left = 0;
    size_t right = input.size() - 1;

    while (left < right){
        if (input[left] == input[right]){
            ++left;
            --right;
        }else{
            return false;
        }
    }

    return true;

}

void solve(char& replacementChar, std::string& stringInput){
    int minCoinsSpent = 0;

    if (isPalindrome(stringInput)){
        std::cout << minCoinsSpent << std::endl;
        return;
    }


    size_t left = 0;
    size_t right = stringInput.size() - 1;

    while (left < right){
        if (stringInput[left] != stringInput[right]){
            if (stringInput[left] == replacementChar && stringInput[right] != replacementChar){
                stringInput[right] = replacementChar;
                ++minCoinsSpent;
            }else if (stringInput[left] != replacementChar && stringInput[right] == replacementChar){
                stringInput[left] = replacementChar;
                ++minCoinsSpent;
            }else{
                stringInput[left] = replacementChar;
                stringInput[right] = replacementChar;
                minCoinsSpent += 2;
            }
        }

        ++left;
        --right;
    }

    std::cout << minCoinsSpent << std::endl;
    return;
}

int main(){

    int testCount;
    std::cin >> testCount;



    for (int i = 0; i < testCount; ++i){
        int stringLength;
        char replacementChar;
        std::string stringInput;

        std::cin >> stringLength >> replacementChar >> stringInput;
        
        solve(replacementChar, stringInput);

    }

    return 0;
}