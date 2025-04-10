#include <iostream>

int main() {
    int a, k, x, y, b;
    bool condition = true;

    // BB1: entry

    // BB2: k=2
    k = 2;

    // BB3: if
    if (condition) {
        // BB4: a=k+2
        a = k + 2;

        // BB6: x=5
        if (true) x = 5;
    } else {
        // BB5: a=k*2
        a = k * 2;

        // BB7: x=8
        if (true) x = 8;
    }

    // BB8: k=a
    k = a;

    // BB9: while
    while (k < 10) {  // Added a condition for the while loop
        // BB10: b=2
        b = 2;

        // BB11: x=a+k
        x = a + k;

        // BB12: y=a*b
        y = a * b;

        // BB13: k++
        k++;
    }

    // BB14: print(a+x)
    std::cout << "Result: " << (a + x) << std::endl;

    // BB15: exit (implicit in C++)
    return 0;
}
