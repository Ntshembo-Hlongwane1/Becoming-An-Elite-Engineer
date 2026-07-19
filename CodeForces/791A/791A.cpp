#include <iostream>

using namespace std;

int main(){

    int a, b;

    cin >> a >> b;

    int res = 0;

    while (a <= b){
        res++;

        a *= 3;
        b *= 2;
    }

    cout << res << endl;

    return 0;
}