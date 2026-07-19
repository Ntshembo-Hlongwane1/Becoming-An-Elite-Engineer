#include <iostream>
#include <string>
#include <unordered_map>

using namespace std;

int main(){

    string name;

    cin >> name;

    unordered_map<char, int> charFreq;

    for (size_t i = 0; i < name.length(); i++){
        charFreq[name[i]] = 1;
    }

    cout << (static_cast<int>(charFreq.size()) % 2 == 0 ? "CHAT WITH HER!" : "IGNORE HIM!") << endl;
    return 0;
}