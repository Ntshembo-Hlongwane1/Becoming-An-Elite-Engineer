#include <iostream>
 
 
int getResult(int n){
    if (n - 100 == 0 || n - 20 == 0 || n - 10 == 0 || n - 5 == 0 || n - 1 == 0){
        return 1;
    };
    
    if (n - 100 > 0){
        return 1 + getResult(n - 100);
    };
    
    if (n - 20 > 0){
        return 1 + getResult(n - 20);
    };
    
    if (n - 10 > 0){
        return 1 + getResult(n - 10);
    };
    
    if (n - 5 > 0){
        return 1 + getResult(n - 5);
    };
    
    if (n - 1){
        return 1 + getResult(n - 1);
    };
    
    return 0;
}
int main(){
    
    int n;
    std::cin >> n;
    
    
    int res = getResult(n);
    
    std::cout << res << std::endl;
    
    return 0;
}