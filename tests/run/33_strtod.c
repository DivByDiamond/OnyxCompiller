// EXPECT: strtod-ok
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
static unsigned long bits(double value) {
    unsigned long result;
    memcpy(&result, &value, 8);
    return result;
}
int main(void) {
    char *end;
    const char *input = " \v\f\r\n\t-0x1.8p+2tail";
    double value;
    errno = 0;
    value = strtod(input, &end);
    if (value != -6 || strcmp(end, "tail") || errno) return 1;
    input = " -nonsense";
    value = strtod(input, &end);
    if (bits(value) || end != input) return 2;
    if (strtod("1e+", &end) != 1 || strcmp(end, "e+")) return 3;
    if (strtod("0x1p-", &end) != 1 || strcmp(end, "p-")) return 4;
    if (strtod("0x.p1", &end) != 0 || strcmp(end, "x.p1")) return 5;
    if (strtod(".5E+2!", &end) != 50 || strcmp(end, "!")) return 6;
    if (bits(strtod("-0.000e999999", &end)) != 0x8000000000000000UL || *end) return 7;
    if (bits(strtod("0.1", NULL)) != 0x3fb999999999999aUL) return 8;
    if (bits(strtod("1.7976931348623157e308", NULL)) != 0x7fefffffffffffffUL) return 9;
    if (bits(strtod("2.2250738585072014e-308", NULL)) != 0x0010000000000000UL) return 10;
    errno = 0;
    if (bits(strtod("0x1p-1074", NULL)) != 1 || errno) return 11;
    errno = 0;
    if (bits(strtod("4.9406564584124654e-324", NULL)) != 1 || errno != ERANGE) return 12;
    errno = 0;
    if (bits(strtod("-1e-9999", &end)) != 0x8000000000000000UL || errno != ERANGE || *end) return 13;
    errno = 0;
    if (bits(strtod("1e9999", NULL)) != 0x7ff0000000000000UL || errno != ERANGE) return 14;
    errno = 0;
    if (bits(strtod("-INFINITY!", &end)) != 0xfff0000000000000UL || strcmp(end, "!") || errno) return 15;
    value = strtod("nan(payload)rest", &end);
    if (value == value || strcmp(end, "rest")) return 16;
    value = strtod("nan(unclosed", &end);
    if (value == value || strcmp(end, "(unclosed")) return 17;
    if (bits(strtod("1.00000000000000011102230246251565404236316680908203125", NULL)) != 0x3ff0000000000000UL) return 18;
    errno = 0;
    if (bits(strtod("2.2250738585072012e-308", NULL)) != 0x0010000000000000UL || errno != ERANGE) return 19;
    printf("strtod-ok\n");
    return 0;
}
