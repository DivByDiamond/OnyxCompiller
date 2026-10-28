// EXPECT: partial-init-ok
#include <stdio.h>

struct payload { int first; long rest[3]; };

static int check(void) {
    volatile char preceding = 19;
    unsigned char bytes[25] = {0};
    int numbers[6] = {3};
    int matrix[2][3] = {{2}};
    struct payload object = {7};
    if (preceding != 19 || numbers[0] != 3 || matrix[0][0] != 2 || object.first != 7) return 1;
    for (int i = 0; i < 25; i++) if (bytes[i]) return 2;
    for (int i = 1; i < 6; i++) if (numbers[i]) return 3;
    for (int i = 0; i < 2; i++) for (int j = 0; j < 3; j++)
        if ((i || j) && matrix[i][j]) return 4;
    for (int i = 0; i < 3; i++) if (object.rest[i]) return 5;
    for (int i = 0; i < 25; i++) bytes[i] = 99;
    for (int i = 0; i < 6; i++) numbers[i] = 99;
    for (int i = 0; i < 2; i++) for (int j = 0; j < 3; j++) matrix[i][j] = 99;
    for (int i = 0; i < 3; i++) object.rest[i] = 99;
    return 0;
}

int main(void) {
    if (check() || check() || check()) return 1;
    printf("partial-init-ok\n");
    return 0;
}
