/*
 * math.c — soft math for libonyxc.
 *
 * Newton-Raphson sqrt, power-series exp/log/sin/cos, and the usual
 * rounding helpers. Precision is "utility grade" (editors, monitors,
 * progress bars) — not libm-grade — but fully adequate for userspace
 * tools and avoids any host libm dependency.
 */
#include "onyxc.h"
#include <math.h>

double fabs(double x) { return x < 0 ? -x : x; }
float fabsf(float x) { return x < 0 ? -x : x; }

double sqrt(double x) {
    if (x < 0) return -(0.0 / 0.0);
    if (x == 0) return 0;
    /* Newton-Raphson, seeded by bit-halving. */
    double r = x > 1 ? x : 1;
    for (int i = 0; i < 60; i++) {
        double nr = 0.5 * (r + x / r);
        if (nr == r) break;
        r = nr;
    }
    return r;
}

float sqrtf(float x) { return (float)sqrt((double)x); }

double floor(double x) {
    if (x >= 0) {
        double i = (double)(long long)x;
        return i;
    }
    double i = (double)(long long)x;
    if (i != x) i -= 1.0;
    return i;
}

float floorf(float x) { return (float)floor((double)x); }

double ceil(double x) {
    if (x <= 0) {
        double i = (double)(long long)x;
        return i;
    }
    double i = (double)(long long)x;
    if (i != x) i += 1.0;
    return i;
}

double round(double x) {
    if (x >= 0) return floor(x + 0.5);
    return ceil(x - 0.5);
}

double fmod(double x, double y) {
    if (y == 0) return 0.0 / 0.0;
    long long q = (long long)(x / y);
    return x - (double)q * y;
}

double exp(double x) {
    /* Handle range via squaring: e^x = (e^(x/2^n))^(2^n). */
    if (x != x) return x;
    int n = 0;
    double base = x;
    while (base > 1 || base < -1) { base *= 0.5; n++; }
    /* Taylor series for small base. */
    double term = 1, sum = 1;
    for (int i = 1; i < 30; i++) {
        term *= base / (double)i;
        sum += term;
    }
    for (int i = 0; i < n; i++) sum *= sum;
    return sum;
}

double log(double x) {
    if (x <= 0) return -(0.0 / 0.0);
    if (x == 1) return 0;
    /* Scale into [0.5, 1) via sqrt halving, then series. */
    int n = 0;
    while (x > 1) { x *= 0.5; n++; }
    while (x < 0.5) { x *= 2; n--; }
    /* x in [0.5, 1): use ln(x) = ln(1-y) with y = 1-x, |y| <= 0.5 */
    double y = 1.0 - x;
    double term = y, sum = 0;
    for (int i = 1; i < 40; i++) {
        sum += term / (double)i;
        term *= y;
    }
    return -sum + n * 0.69314718055994530942;
}

double log10(double x) {
    return log(x) / 2.30258509299404568402;
}

double pow(double x, double y) {
    if (y == 0) return 1;
    if (x == 0) return 0;
    if (y == (double)(long long)y) {
        /* Integer exponent: fast path. */
        long long e = (long long)y;
        int neg = e < 0;
        if (neg) e = -e;
        double r = 1, b = x;
        while (e) {
            if (e & 1) r *= b;
            b *= b;
            e >>= 1;
        }
        return neg ? 1.0 / r : r;
    }
    /* Fractional: x^y = exp(y * ln x). */
    return exp(y * log(x));
}

/* sin/cos via Taylor with range reduction (x mod 2π → [-π, π]). */
static double reduce_pi(double x) {
    double two_pi = 6.28318530717958647692;
    double r = fmod(x, two_pi);
    if (r > 3.14159265358979323846) r -= two_pi;
    if (r < -3.14159265358979323846) r += two_pi;
    return r;
}

double sin(double x) {
    x = reduce_pi(x);
    double x2 = x * x;
    double term = x, sum = x;
    for (int i = 1; i < 15; i++) {
        term *= -x2 / (double)(2 * i * (2 * i + 1));
        sum += term;
    }
    return sum;
}

double cos(double x) {
    x = reduce_pi(x);
    double x2 = x * x;
    double term = 1, sum = 1;
    for (int i = 1; i < 15; i++) {
        term *= -x2 / (double)((2 * i - 1) * 2 * i);
        sum += term;
    }
    return sum;
}

double tan(double x) {
    double c = cos(x);
    if (c == 0) return 0.0 / 0.0;
    return sin(x) / c;
}

double atan2(double y, double x) {
    if (x == 0) {
        if (y > 0) return 1.57079632679489661923;
        if (y < 0) return -1.57079632679489661923;
        return 0;
    }
    double t = y / x;
    /* atan via Padé-ish series on reduced range. */
    int neg = t < 0;
    if (neg) t = -t;
    int inv = t > 1;
    if (inv) t = 1.0 / t;
    double t2 = t * t;
    double sum = t;
    double term = t;
    for (int i = 1; i < 20; i++) {
        term *= -t2;
        sum += term / (double)(2 * i + 1);
    }
    if (inv) sum = 1.57079632679489661923 - sum;
    if (neg) sum = -sum;
    if (x < 0) {
        if (y >= 0) sum += 3.14159265358979323846;
        else sum -= 3.14159265358979323846;
    }
    return sum;
}
