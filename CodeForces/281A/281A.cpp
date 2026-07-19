#include <iostream>
#include <string>

using namespace std;

int main(){

    string input;

    cin >> input;

    cout << char(toupper(input[0])) << input.substr(1) << endl;
    
    return 0;
}