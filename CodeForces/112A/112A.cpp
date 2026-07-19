#include <iostream>
#include <string>

using namespace std;

int main(){

    string word1;
    string word2;

    cin >> word1 >> word2;

    if (word1.length() < word2.length()){
        cout << -1 << endl;
    } else if (word2.length() < word1.length()){
        cout << 1 << endl;
    }else {
        for (size_t i = 0; i < word1.length(); i++){
            char lowerWord1 = tolower(static_cast<unsigned char>(word1[i]));
            char lowerWord2 = tolower(static_cast<unsigned char>(word2[i]));

            if (lowerWord1 != lowerWord2){
                if (lowerWord1 < lowerWord2){
                    cout << -1 << endl;
                }else{
                    cout << 1 << endl;
                };
                
                return 0;
            };
        }

        cout << 0 << endl;
    }

    return 0;    
};