int main() {

    int a = 7;
    int b = 6;

    for (int i = 0; i < 10; i++) {
        int c = a + i;
        int d = b + 4;

        for (int j = 0; j < 10; j++) {
            int e = c + a;
            int f = d + j;
        }
    }

    return 0;
}
