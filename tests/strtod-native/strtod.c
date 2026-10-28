#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <math.h>
double onyx_strtod(const char *, char **);
static int parser_errno;
int *___errno_location(void) { return &parser_errno; }
static int compare(const char *input) {
    char *host_end, *own_end;
    union { double value; uint64_t bits; } host, own;
    int host_errno;
    errno = 0; host.value = strtod(input, &host_end); host_errno = errno;
    parser_errno = 0; own.value = onyx_strtod(input, &own_end);
    if (((!isnan(host.value) || !isnan(own.value)) && host.bits != own.bits) ||
        (isnan(host.value) && signbit(host.value) != signbit(own.value)) ||
        host_end - input != own_end - input || host_errno != parser_errno) {
        fprintf(stderr, "input=%.150s\nhost=%a end=%ld errno=%d\nours=%a end=%ld errno=%d\n",
            input, host.value, (long)(host_end - input), host_errno,
            own.value, (long)(own_end - input), parser_errno);
        return 1;
    }
    return 0;
}
int main(void) {
    const char *cases[] = {"", " ", "-", " +.", " -abc", "123abc", ".5", "1.",
        "1e", "1e+", "1e-", "1e+3rest", "\v\f\r\n\t -0", "-0.000e99999", "1e99999999999999999999",
        "-1e-99999999999999999999", "0x1.8p+2", "-0X.8P-1tail", "0x", "0x.p1", "0x1p+", "0x1.2",
        "1.7976931348623157e308", "1.7976931348623159e308", "2.2250738585072014e-308", "2.2250738585072012e-308", "0x0.fffffffffffff8p-1022",
        "4.9406564584124654e-324", "2.4703282292062327e-324", "2.4703282292062328e-324",
        "0x1p-1074", "0x1p-1075", "0x1.fffffffffffffp1023", "0x1.fffffffffffff8p1023",
        "INF", "-Infinity!", "+infinite", "nan", "-NaN(payload)", "nan(unclosed", "nan(a-b)",
        "0.1000000000000000055511151231257827021181583404541015625",
        "1.00000000000000011102230246251565404236316680908203125"};
    char input[2200];
    uint64_t random = 51;
    unsigned count = 0;
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        if (compare(cases[i])) return 1;
        count++;
    }
    strcpy(input, "1.00000000000000011102230246251565404236316680908203125");
    i = strlen(input); memset(input + i, '0', 1000); input[i + 1000] = '1'; input[i + 1001] = 0;
    if (compare(input)) return 1;
    strcpy(input, "0."); memset(input + 2, '0', 1500); strcpy(input + 1502, "1e1501");
    if (compare(input)) return 1;
    input[0] = '1'; memset(input + 1, '0', 1500); strcpy(input + 1501, "e-1500");
    if (compare(input)) return 1;
    strcpy(input, "0x1.00000000000008");
    i = strlen(input); memset(input + i, '0', 1000); strcpy(input + i + 1000, "1p0");
    if (compare(input)) return 1;
    for (i = 0; i < 2048; ++i) {
        union { double value; uint64_t bits; } value;
        random = random * 6364136223846793005ULL + 1442695040888963407ULL;
        value.bits = random;
        snprintf(input, sizeof(input), "%.17g", value.value);
        if (compare(input)) return 1;
        snprintf(input, sizeof(input), "%a", value.value);
        if (compare(input)) return 1;
        count += 2;
    }
    printf("native strtod: %u differential cases PASS\n", count + 4);
    return 0;
}
