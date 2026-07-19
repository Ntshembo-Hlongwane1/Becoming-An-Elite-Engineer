#include <iostream>
#include <string>
#include <algorithm>
#include <vector>


using namespace std;

int main(){

    string input;
    
    cin >> input;

    vector<char> nums;

    for (const auto& ch : input){

        if (tolower(static_cast<unsigned char>(ch)) == '+'){
            continue;
        };

        nums.emplace_back(ch);
    };

    string res = "";

    sort(nums.begin(), nums.end(), [](char a, char b){
        return a < b;
    });


    for (size_t i = 0; i < nums.size(); i++){
        if (i > 0){
            res += '+';
        }

        res += nums[i];
    }
    cout << res << endl;

    return 0;
}