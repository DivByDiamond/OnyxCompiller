#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../libonyxc/src/format/internal.h"
struct capture { char text[4096]; size_t length; };
static int emit(void *context, char ch) {
    struct capture *out = context;
    if (out->length == sizeof(out->text) - 1) return -1;
    out->text[out->length++] = ch;
    out->text[out->length] = 0;
    return 0;
}
static int compare(uint64_t bits, char spec, int precision, int width, unsigned flags) {
    union { uint64_t bits; double value; } value;
    struct capture actual = {{0}, 0};
    char expected[4096], format[64], flag_text[6];
    int i = 0, n;
    value.bits = bits;
    if (flags & ONYX_FMT_MINUS) flag_text[i++] = '-';
    if (flags & ONYX_FMT_PLUS) flag_text[i++] = '+';
    if (flags & ONYX_FMT_SPACE) flag_text[i++] = ' ';
    if (flags & ONYX_FMT_HASH) flag_text[i++] = '#';
    if (flags & ONYX_FMT_ZERO) flag_text[i++] = '0';
    flag_text[i] = 0;
    snprintf(format, sizeof(format), "%%%s%d.%d%c", flag_text, width, precision, spec);
    n = snprintf(expected, sizeof(expected), format, value.value);
    if (_onyx_format_float(value.value, spec, precision, width, flags, emit, &actual) < 0 ||
        n != (int)actual.length || strcmp(expected, actual.text)) {
        fprintf(stderr, "bits=%016llx format=%s\nexpected=%s\nactual=%s\n",
                (unsigned long long)bits, format, expected, actual.text);
        return 1;
    }
    return 0;
}
int main(void) {
    uint64_t values[] = {0, 0x8000000000000000ULL, 0x3ff0000000000000ULL,
        0x3fb999999999999aULL, 0x4004000000000000ULL, 0x400c000000000000ULL,
        0x4023ffffffffffffULL, 0x3ee4f8b588e368f1ULL, 0x4415af1d78b58c40ULL,
        0x7fefffffffffffffULL, 0x0010000000000000ULL, 1, 0xfffffffffffffULL,
        0x7ff0000000000000ULL, 0xfff0000000000000ULL, 0x7ff8000000000000ULL,
        0xfff8000000000000ULL};
    int precisions[] = {0, 1, 2, 6, 15, 17, 99, 200};
    unsigned flags[] = {0, ONYX_FMT_PLUS | ONYX_FMT_ZERO, ONYX_FMT_HASH,
        ONYX_FMT_MINUS | ONYX_FMT_SPACE};
    const char *specs = "fFeEgG";
    size_t i, j, k, m;
    unsigned count = 0;
    uint64_t random = 81;
    for (i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
        for (j = 0; j < sizeof(precisions) / sizeof(precisions[0]); ++j)
            for (k = 0; specs[k]; ++k)
                for (m = 0; m < sizeof(flags) / sizeof(flags[0]); ++m) {
                    if (compare(values[i], specs[k], precisions[j], 23, flags[m])) return 1;
                    count++;
                }
    for (i = 0; i < 64; ++i) {
        random = random * 6364136223846793005ULL + 1442695040888963407ULL;
        for (k = 0; k < 3; ++k) {
            if (compare(random, "feg"[k], 17, 0, 0)) return 1;
            count++;
        }
    }
    printf("native float formatting: %u differential cases PASS\n", count);
    return 0;
}
