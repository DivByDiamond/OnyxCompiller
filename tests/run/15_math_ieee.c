// EXPECT: math-ieee-ok
#include <stdio.h>
#include <math.h>
#include <string.h>

static unsigned long bits(double x) {
    unsigned long result;
    memcpy(&result, &x, 8);
    return result;
}

int main(void) {
    int exponent;
    double integer;
    double tiny = ldexp(1.0, -1074);
    if (bits(tiny) != 1) return 1;
    if (frexp(tiny, &exponent) != 0.5 || exponent != -1073) return 2;
    if (ldexp(tiny, 1074) != 1.0) return 3;
    if (bits(frexp(-0.0, &exponent)) != 0x8000000000000000UL) return 4;
    if (modf(-3.75, &integer) != -0.75 || integer != -3.0) return 5;
    if (bits(modf(-2.0, &integer)) != 0x8000000000000000UL) return 6;
    if (modf(INFINITY, &integer) != 0.0 || !isinf(integer)) return 7;
    if (!isnan(modf(NAN, &integer)) || !isnan(integer)) return 8;
    if (log2(tiny) != -1074.0 || log2(8.0) != 3.0) return 9;
    if (fabs(asin(0.5) - M_PI / 6.0) > 1e-12) return 10;
    if (fabs(acos(-0.5) - 2.0 * M_PI / 3.0) > 1e-12) return 11;
    if (!isnan(acos(2.0)) || !isinf(HUGE_VAL)) return 12;
    if (bits(asin(-0.0)) != 0x8000000000000000UL) return 13;
    if (asin(tiny) != tiny || asin(-tiny) != -tiny) return 14;
    if (!isinf(ldexp(1.0, 2147483647))) return 15;
    if (bits(ldexp(-1.0, -2147483647 - 1)) != 0x8000000000000000UL) return 16;
    printf("math-ieee-ok\n");
    return 0;
}
