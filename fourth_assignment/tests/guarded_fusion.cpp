int foo(int a, int b, int x, int n) {
    int i = 0;
    int k = 0;
    if (n > 0) {
        do {
            // Loop body
            i += 1;
        } while (i < 10);
    }

    if (n > 0) {
        do {
            // Loop body
            // It can be a problem with scalars
            k++;
            int f = 3 * k;
        } while (k < 10);
    }

    return 0;
}
