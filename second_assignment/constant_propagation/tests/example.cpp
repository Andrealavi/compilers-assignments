#include <iostream>

int main() {
    // entry point
    int k = 2;
    int a, x, y, b;

    // if statement (no condition shown, assumed to be true)
    if (k > 1) {
        // Left branch
        a = k + 2;  // a = 4
        x = 5;
    } else {
        // Right branch
        a = k * 2;  // a = 4
        x = 8;
    }

    // Merge point
    k = a;  // k = 4

    // while loop (no condition shown, assumed to be true)
    while (true) {
        b = 2;
        x = a + k;  // x = 4 + 4 = 8 first iteration
        y = a * b;  // y = 4 * 2 = 8 first iteration
        k++;        // k = 5 after first iteration

        // Branch leading to exit
        std::cout << (a + x) << std::endl;  // prints 4 + 8 = 12 first iteration
        break;  // Since there's an exit, we need to break out of the loop
    }

    return 0;
}
