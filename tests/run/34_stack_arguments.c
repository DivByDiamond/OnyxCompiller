// EXPECT: stack-arguments-ok
#include <stdio.h>
static long nine(long a,long b,long c,long d,long e,long f,long g,long h,long i) {
    return a+b+c+d+e+f+g+h+i;
}
static double ten(double a,double b,double c,double d,double e,double f,double g,double h,double i,double j) {
    return a+b+c+d+e+f+g+h+i+j;
}
static float two(float a, float b) { return a+b; }
int main(void) {
    if (nine(1,2,3,4,5,6,7,8,9) != 45) return 1;
    if (ten(1,2,3,4,5,6,7,8,9,10) != 55.0) return 2;
    if (two(1.25,2.5) != 3.75) return 3;
    printf("stack-arguments-ok\n"); return 0;
}
