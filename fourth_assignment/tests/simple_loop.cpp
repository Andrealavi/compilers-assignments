#include <iostream>

int main(void) {

    int a = 0;
    int b = 0;

    for (int i = 0; i < 10; i++){
        if (i>3) {
            a++;
        } else {
            a--;
        }
    }

    for (int i = 0; i < 10; i++) {
        b++;
    }

    for (int i = 0; i < 10; i++) {
        int c = a + b;
    }

    std::cout << a << " " << b << std::endl;

}
