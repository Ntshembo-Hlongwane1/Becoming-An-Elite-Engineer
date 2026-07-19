#include <iostream>

int main() {
    int n;

    std::cin >> n;
    
    int result = 0;

    for (int i = 0; i < n; i++){
        std::string operation;
        std::cin >> operation;

        bool isAddOp = false;

        for (auto it = operation.begin(); it != operation.end(); ++it){
            char ch = *it;
            
            if (ch == 'X' || ch == 'x'){
                continue;
            }else if (ch == '-' && *std::next(it) == '-'){
                isAddOp = false;
            }else if (ch == '+' && *std::next(it) == '+'){
                isAddOp = true;
            }else{
                continue;
            };
        };


        if (isAddOp){
            result++;
        }else{
            result--;
        };
    };

    std::cout << result << std::endl;

    return 0;
}