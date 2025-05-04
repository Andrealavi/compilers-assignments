int main() {

    int a = 2, b = 3;

    for (int i = 0; i < 10; i++) {
        int c = a + b;
        int d = a + 4;

        a = b + 1;

        int e;
        if (a > 3) {
            e = a + b * 2;
        } else {
            e = d + b / 2;
        }

        int f = c + d;
    }

    return 0;
}
