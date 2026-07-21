char arr[2][6] = { "hello", "world" };

int main(void) {
    if (arr[0][0] != 'h') return 1;
    if (arr[0][4] != 'o') return 2;
    if (arr[1][0] != 'w') return 3;
    if (arr[1][4] != 'd') return 4;
    return 0;
}
