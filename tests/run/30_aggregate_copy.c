// EXPECT: aggregate-copy-ok
#include <stdio.h>
struct value { long a, b, c; };
struct bytes { char data[11]; };
int main(void) {
    struct value a = {1,2,3}, b = {4,5,6};
    b = a;
    if (b.a != 1 || b.b != 2 || b.c != 3) return 1;
    struct bytes x = {"hello-world"}, y = {"xxxxxxxxxxx"};
    y = x;
    if (y.data[0] != 'h' || y.data[10] != 'd') return 2;
    printf("aggregate-copy-ok\n"); return 0;
}
