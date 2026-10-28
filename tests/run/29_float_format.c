// EXPECT: float-format-ok
#include <stdio.h>
#include <string.h>
#include <float.h>
static int equal(const char *format, double value, const char *expected) {
    char text[512];
    int n = snprintf(text, sizeof(text), format, value);
    return n == (int)strlen(expected) && strcmp(text, expected) == 0;
}
int main(void) {
    char text[128];
    int i;
    union { unsigned long bits; double value; } special;
    if (!equal("%.3e", 12345.0, "1.234e+04")) return 1;
    if (!equal("%.3g", 12345.0, "1.23e+04")) return 2;
    if (!equal("%.6g", 0.0001, "0.0001") || !equal("%.6g", 0.00001, "1e-05")) return 3;
    if (!equal("%.2g", 9.99, "10") || !equal("%.2g", 99.9, "1e+02")) return 4;
    if (!equal("%#.4g", 12.0, "12.00") || !equal("%#.0f", 2.0, "2.")) return 5;
    if (!equal("%.0f", 2.5, "2") || !equal("%.0f", 3.5, "4")) return 6;
    if (!equal("%-10.2E", 1.25, "1.25E+00  ")) return 7;
    if (!equal("%.17g", 0.1, "0.10000000000000001")) return 8;
    if (!equal("%.17g", DBL_MAX, "1.7976931348623157e+308")) return 9;
    if (!equal("%.17g", DBL_MIN, "2.2250738585072014e-308")) return 10;
    special.bits = 1;
    if (!equal("%.17g", special.value, "4.9406564584124654e-324")) return 11;
    special.bits = 0x8000000000000000UL;
    if (!equal("%+010.2f", special.value, "-000000.00")) return 12;
    special.bits = 0x7ff0000000000000UL;
    if (!equal("%G", special.value, "INF")) return 13;
    special.bits = 0x7ff8000000000000UL;
    if (!equal("%g", special.value, "nan")) return 14;
    if (snprintf(text, sizeof(text), "%.99f", 1.0) != 101) return 15;
    if (text[0] != '1' || text[1] != '.' || text[101]) return 16;
    for (i = 2; i < 101; ++i) if (text[i] != '0') return 17;
    if (snprintf(text, 4, "%.3e", 12345.0) != 9 || strcmp(text, "1.2")) return 18;
    printf("float-format-ok\n");
    return 0;
}
