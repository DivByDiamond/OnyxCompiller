/*
 * math.h — freestanding math subset for libonyxc.
 *
 * Soft implementations (no FPU libc dependency): enough for editors,
 * monitors and general utilities. All functions are simple C over
 * float/double arithmetic already supported by the compiler's F/D
 * codegen (fadd/fsub/fmul/fdiv/fsqrt).
 */
#ifndef _LIBONYXC_MATH_H
#define _LIBONYXC_MATH_H

#define M_E        2.7182818284590452354
#define M_LOG2E    1.4426950408889634074
#define M_LOG10E   0.43429448190325182765
#define M_LN2      0.69314718055994530942
#define M_LN10     2.30258509299404568402
#define M_PI       3.14159265358979323846
#define M_PI_2     1.57079632679489661923
#define M_PI_4     0.78539816339744830962
#define M_1_PI     0.31830988618379067154
#define M_2_PI     0.63661977236758134308
#define M_SQRT2    1.41421356237309504880
#define M_SQRT1_2  0.70710678118654752440

#define HUGE_VAL  1e308
#define INFINITY  (1.0 / 0.0)
#define NAN       (0.0 / 0.0)

#define isnan(x)  ((x) != (x))
#define isinf(x)  ((x) == INFINITY || (x) == -INFINITY)
#define isfinite(x) (!isnan(x) && !isinf(x))

double sqrt(double x);
double fabs(double x);
double floor(double x);
double ceil(double x);
double fmod(double x, double y);
double pow(double x, double y);
double exp(double x);
double log(double x);
double log10(double x);
double sin(double x);
double cos(double x);
double tan(double x);
double atan2(double y, double x);
double round(double x);

float sqrtf(float x);
float fabsf(float x);
float floorf(float x);

#endif /* _LIBONYXC_MATH_H */
