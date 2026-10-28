/* Binary64 decomposition and Lua's missing elementary math operations.
 * Bit access uses memcpy so it does not depend on aliasing extensions. */
#include "onyxc.h"
#include <math.h>
#include <errno.h>

static unsigned long double_bits(double x) {
    unsigned long bits;
    memcpy(&bits, &x, sizeof(bits));
    return bits;
}

static double bits_double(unsigned long bits) {
    double x;
    memcpy(&x, &bits, sizeof(x));
    return x;
}

double frexp(double x, int *exponent) {
    unsigned long bits = double_bits(x);
    int e = (int)((bits >> 52) & 2047);
    *exponent = 0;
    if ((bits & 0x7fffffffffffffffUL) == 0 || e == 2047) return x;
    int correction = 0;
    if (e == 0) {
        x *= 18014398509481984.0; /* 2^54 normalizes every subnormal. */
        bits = double_bits(x);
        e = (int)((bits >> 52) & 2047);
        correction = -54;
    }
    *exponent = e - 1022 + correction;
    return bits_double((bits & 0x800fffffffffffffUL) | (1022UL << 52));
}

double ldexp(double x, int exponent) {
    unsigned long magnitude = double_bits(x) & 0x7fffffffffffffffUL;
    if (!magnitude || magnitude >= 0x7ff0000000000000UL) return x;
    /* Each intermediate moves in the requested direction. No integer
     * negation of exponent is needed, including when it is INT_MIN. */
    while (exponent > 1023) {
        x *= bits_double(2046UL << 52);
        exponent -= 1023;
        if (isinf(x)) { errno = ERANGE; return x; }
    }
    while (exponent < -1022) {
        /* Leave 53 exponent bits of headroom, so a potentially nonzero
         * final subnormal is rounded only by the last multiplication. */
        x *= bits_double(54UL << 52); /* 2^-969 */
        exponent += 969;
        if (x == 0) { errno = ERANGE; return x; }
    }
    x *= bits_double((unsigned long)(exponent + 1023) << 52);
    if (isinf(x) || x == 0) errno = ERANGE;
    return x;
}

double modf(double x, double *integer) {
    unsigned long bits = double_bits(x);
    unsigned long sign = bits & 0x8000000000000000UL;
    int e = (int)((bits >> 52) & 2047) - 1023;
    if (e < 0) {
        *integer = bits_double(sign);
        return x;
    }
    if (e >= 52) {
        *integer = x;
        if (isnan(x)) return x;
        return bits_double(sign);
    }
    unsigned long mask = (1UL << (52 - e)) - 1;
    *integer = bits_double(bits & ~mask);
    if (!(bits & mask)) return bits_double(sign);
    return x - *integer;
}

/* atan(t), t >= 0. Reciprocal and half-angle reduction bound the
 * alternating series argument by sqrt(2)-1. */
static double positive_atan(double t) {
    int reciprocal = t > 1.0;
    if (reciprocal) t = 1.0 / t;
    t = t / (1.0 + sqrt(1.0 + t * t));
    double term = t;
    double sum = t;
    double square = t * t;
    for (int i = 1; i < 24; i++) {
        term *= -square;
        sum += term / (double)(2 * i + 1);
    }
    sum *= 2.0;
    return reciprocal ? M_PI_2 - sum : sum;
}

double asin(double x) {
    if (isnan(x)) return x;
    if (x > 1.0 || x < -1.0) { errno = EDOM; return NAN; }
    if (x == 0.0) return x;
    if (x == 1.0) return M_PI_2;
    if (x == -1.0) return -M_PI_2;
    double magnitude = x < 0 ? -x : x;
    /* asin(x) rounds to x here; avoid losing a smallest subnormal during
     * the half-angle reduction in positive_atan. */
    if (magnitude < 1e-8) return x;
    double result = positive_atan(magnitude / sqrt((1.0 - magnitude) * (1.0 + magnitude)));
    return x < 0 ? -result : result;
}

double acos(double x) {
    if (isnan(x)) return x;
    if (x > 1.0 || x < -1.0) { errno = EDOM; return NAN; }
    if (x == 1.0) return 0.0;
    if (x == -1.0) return M_PI;
    if (x == 0.0) return M_PI_2;
    double magnitude = x < 0 ? -x : x;
    double result = positive_atan(sqrt((1.0 - magnitude) * (1.0 + magnitude)) / magnitude);
    return x < 0 ? M_PI - result : result;
}

double log2(double x) {
    if (isnan(x)) return x;
    if (x == 0.0) { errno = ERANGE; return -INFINITY; }
    if (x < 0.0) { errno = EDOM; return NAN; }
    if (isinf(x)) return x;
    int exponent;
    double m = frexp(x, &exponent) * 2.0;
    exponent--;
    /* log(m) = 2*(t + t^3/3 + ...), |t| <= 1/3. */
    double t = (m - 1.0) / (m + 1.0);
    double term = t;
    double sum = t;
    double square = t * t;
    for (int i = 1; i < 32; i++) {
        term *= square;
        sum += term / (double)(2 * i + 1);
    }
    return (double)exponent + 2.0 * sum * M_LOG2E;
}
