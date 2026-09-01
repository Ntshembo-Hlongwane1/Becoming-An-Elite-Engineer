#include <iostream>
#include <string>
 
int main(){
    std::string input;
    std::getline(std::cin, input);
 
    int usedLetters = 0;
    int uniqueSetcount = 0;
 
    for (char c : input){
        if (c >= 'a' && c <= 'z'){
            int bit = c - 'a';
 
            if ((usedLetters & (1 << bit)) == 0){
                uniqueSetcount++;
                usedLetters |= (1 << bit);
            }
        }
    }
 
    std::cout << uniqueSetcount << '\n';
 
    return 0;
}