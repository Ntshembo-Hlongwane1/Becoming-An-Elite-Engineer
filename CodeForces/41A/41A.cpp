#include <iostream>
#include <string>

int main(){
    
    std::string s;
    std::string t;

    std::cin >> s >> t;
    
    if (s.length() != t.length()){
        std::cout << "NO" << std::endl;
        return 0;
    };


    int Stracker = 0;
    for (int i = t.length() - 1; i >= 0; i--){
        if (t[i] != s[Stracker]){
            std::cout << "NO" << std::endl;
            return 0;
        }

        Stracker++;
    }

    std::cout << "YES" << std::endl;

    

    return 0;
}