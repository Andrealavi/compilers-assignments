#include <iostream>

int main() {
    int a, b, x, y;

    // BB1: entry
    a = 2, b = 4;

    // BB2: condition (a != b)
    if (a != b) {
        // BB3: true branch - x = b - a
        x = b - a;

        // BB4: x = a - b
        x = a - b;
    } else {
        // BB5: false branch - y = b - a
        y = b - a;

        // BB6: a = 0
        a = 0;

        // BB7: x = a - b
        x = a - b;
    }

    // BB8: exit
    std::cout << "Final values: a = " << a << ", b = " << b << ", x = " << x << std::endl;

    return 0;
}
