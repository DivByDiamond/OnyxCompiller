// EXPECT: add=30 mul=200
#include <stdio.h>
static int add(int a, int b) { return a + b; }
static int mul(int a, int b) { return a * b; }
int main(void) {
    int (*op)(int, int) = add;
    printf("add=%d ", op(10, 20));
    op = mul;
    printf("mul=%d\n", op(10, 20));
    return 0;
}
