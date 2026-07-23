#include <iostream>
#include <cmath>


void printMask(int mask){
    for (int i = 0; i < 10; i++){
        if ((mask & (1 << i)) == 0){
            std::cout << i << ' ';
        }
    }

    std::cout << "\n";
}

bool isBeautiful(int n) {
    int mask = 0;
    while (n > 0) {
        int d = n % 10;
        if (mask & (1 << d)) return false;   // duplicate found
        mask |= (1 << d);
        n /= 10;
    }
    return true;
}

int getDigitCount(int num){
    int count = 0;

    while (num > 0){
        count++;
        num /= 10;
    }

    return count;
}


int main(){
    
    int input;
    std::cin >> input;

    if (!isBeautiful(input)){
        int next = input;

        do {
            next++;
        }while(!isBeautiful(next));
        return 0;
    }


    int temp = input;
    int usedMask = 0;


    
    while(temp > 0){
        int digit = temp % 10;
        usedMask |= (1 << digit);
        temp /= 10;
    };
    

    int processed = 0;
    while(input > 0){
        int digit = input % 10;
        input /= 10;
        processed++;

        usedMask &= ~(1 << digit);

        for (int i = 0; i  < 10; i++){
            if ((usedMask & (1 << i)) == 0){
                if (i > digit){

                    int suffixCount = processed - 1;


                    if (input != 0){
                        std::cout << input;
                    }

                    std::cout << i;
                    int printed = 0;
                    for (int j = 0; j < 10 && printed < suffixCount; j++){
                        if ((usedMask & (1 << j)) == 0 && j != i){
                            printed++;
                            std::cout << j;
                        }

                    }

                    return 0;
                };
            }
        }
    }
    
    
    
    
    return 0;
}




