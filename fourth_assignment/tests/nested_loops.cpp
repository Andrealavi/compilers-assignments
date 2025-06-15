int main() {

    int a[3][3];
    int b[3][3];

    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            a[i][j] = i+j;
        }
    }

    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            b[i][j] = a[i][j+1];
        }
    }

    return 0;
}
