// EXPECT: unary-promotions-ok
#include <stdio.h>
static long widen(long value) { return value; }
static unsigned identity(unsigned value) { return value; }
int main(void) {
    unsigned bare = -3;
    unsigned int explicit_type = -3;
    if (!(bare > 0) || bare != explicit_type || identity(-3) != explicit_type) return 1;
    unsigned int zero = 0;
    if (widen(~zero) != 4294967295L || widen(-1U) != 4294967295L) return 2;
    if (widen(-explicit_type) != 3 || widen(~explicit_type) != 2) return 3;
    unsigned char byte = 255;
    signed char negative = -2;
    unsigned short half = 65535;
    if (-byte != -255 || ~byte != -256 || ~negative != 1) return 4;
    if (-half != -65535 || ~half != -65536 || +byte != 255) return 5;
    if (sizeof(+byte) != sizeof(int) || sizeof(-byte) != sizeof(int) ||
        sizeof(~half) != sizeof(int)) return 6;
    double positive_zero = 0.0, negative_zero = -0.0;
    double nonzero = -0.5;
    float float_zero = -0.0;
    if (!(!positive_zero) || !(!negative_zero) || !(!float_zero) || !nonzero) return 7;
    double nan = 0.0 / positive_zero;
    if (!nan || !!positive_zero || !!nonzero != 1) return 8;
    printf("unary-promotions-ok\n");
    return 0;
}
