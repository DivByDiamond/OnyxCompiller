// EXPECT: 1 2 3 4 5 6 7 8 9 10
// EXPECT: varargs-stack-ok
#include <stdio.h>
#include <stdarg.h>
static int total(int n, ...) {
    va_list ap, copy;
    va_start(ap, n); va_copy(copy, ap);
    int sum = 0;
    for (int i = 0; i < n; i++) sum += va_arg(ap, int);
    for (int i = 0; i < n; i++) sum -= va_arg(copy, int);
    va_end(copy); va_end(ap); return sum;
}
static double floating(int n, ...) {
    va_list ap; va_start(ap, n);
    double sum = 0;
    for (int i = 0; i < n; i++) sum += va_arg(ap, double);
    va_end(ap); return sum;
}
int main(void) {
    if (total(10,1,2,3,4,5,6,7,8,9,10) != 0) return 1;
    if (floating(10,1.0,2.0,3.0,4.0,5.0,6.0,7.0,8.0,9.0,10.0) != 55) return 2;
    printf("%d %d %d %d %d %d %d %d %d %d\n",1,2,3,4,5,6,7,8,9,10);
    printf("varargs-stack-ok\n"); return 0;
}
