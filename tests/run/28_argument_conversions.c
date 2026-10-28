// EXPECT: conversions-ok
#include <stdio.h>
static int mixed(int a, double b, long c) { return a == 1 && b == 505.0 && c == 136; }
static double number(void) { return 42; }
static float narrow(double x) { return x; }
static int integer(int x) { return x; }
int main(void) {
    if (!mixed(1, 505, 136)) return 1;
    if (number() != 42.0 || narrow(2.5) != 2.5) return 2;
    if (integer(3.75) != 3) return 3;
    double x = 5;
    int y = 4.75;
    if (x != 5.0 || y != 4) return 4;
    printf("conversions-ok\n"); return 0;
}
