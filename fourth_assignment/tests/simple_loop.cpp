#include <iostream>

int main(void) {

    int a = 0;
    int b = 0;
    int c = 0;

    for (int i = 0; i < 10; i++){
        if (i>3) {
            a++;
        } else {
            a--;
        }
    }

    for (int i = 0; i < 10; i++) {
        b = i+1;
    }

    for (int i = 0; i < 10; i++) {
        c = c + a + b;
    }

    std::cout << a << " " << b << " " << c << std::endl;

}
