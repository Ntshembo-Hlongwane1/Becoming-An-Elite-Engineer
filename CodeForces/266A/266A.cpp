#include <iostream>
#include <string>

int main() {
    int n;
    std::string s;
    std::cin >> n >> s; 

    int res = 0;
    
    for (auto it = s.begin() + 1; it != s.end(); ++it){
        if (*it == *(it - 1)){
            res++;
        }
    }

    std::cout << res << std::endl;

    return 0;
}