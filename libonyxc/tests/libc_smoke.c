/* Smoke test for libonyxc v0.5.
 * Build (host):  gcc -std=c99 -I include/core -I include/io -I include/ctype \
 *                    -o /tmp/onyx_libc_test src/io/stdio.c src/io/stdlib.c src/io/string.c \
 *                    src/ctype/ctype.c src/io/time.c src/io/strerror.c \
 *                    tests/libc_smoke.c -fno-builtin-vscanf -fno-builtin-vfprintf
 */
#include "onyxc.h"
#include <assert.h>

int test_sprintf_basic(void) {
    char buf[64];
    sprintf(buf, "Hello, %s! count=%d 0x%lx pi=%d.%d",
            "world", 42, 0xdeadbeefL, 3, 14);
    return strcmp(buf, "Hello, world! count=42 0xdeadbeef pi=3.14") == 0;
}

int test_snprintf_trunc(void) {
    char buf[8];
    int n = snprintf(buf, sizeof(buf), "0123456789ABCDEF");
    return n == 16 && strcmp(buf, "0123456") == 0;
}

int test_sscanf_int(void) {
    int a = -1, b = -1;
    int n = sscanf("42 17", "%d %d", &a, &b);
    return n == 2 && a == 42 && b == 17;
}

int test_sscanf_hex(void) {
    unsigned int v = 0;
    int n = sscanf("0xff", "%x", &v);
    return n == 1 && v == 255;
}

int test_sscanf_string(void) {
    char buf[32];
    int n = sscanf("hello world", "%s", buf);
    return n == 1 && strcmp(buf, "hello") == 0;
}

int test_strerror(void) {
    errno = ENOENT;
    char *s = strerror(errno);
    return strcmp(s, "No such file or directory") == 0;
}

int test_strftime(void) {
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = 124; tm.tm_mon = 7; tm.tm_mday = 21;
    tm.tm_hour = 12; tm.tm_min = 30; tm.tm_sec = 45;
    tm.tm_wday = 3; tm.tm_yday = 234;
    char buf[64];
    size_t n = strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return n == 19 && strcmp(buf, "2024-08-21 12:30:45") == 0;
}

int cmp_int(const void *a, const void *b) {
    int x = *(const int *)a;
    int y = *(const int *)b;
    return x - y;
}

int test_qsort(void) {
    int a[] = {5, 2, 8, 1, 9, 3, 7, 4, 6, 0};
    qsort(a, 10, sizeof(int), cmp_int);
    for (int i = 0; i < 10; i++) {
        if (a[i] != i) return 0;
    }
    return 1;
}

int test_bsearch(void) {
    int a[] = {1, 3, 5, 7, 9, 11, 13, 15, 17, 19};
    int key = 13;
    int *p = bsearch(&key, a, 10, sizeof(int), cmp_int);
    return p && *p == 13;
}

int test_strstr_strpbrk(void) {
    char *s = strstr("hello world", "wor");
    if (!s || strcmp(s, "world") != 0) return 0;
    char *p = strpbrk("abc,def;ghi", ",;");
    if (!p || *p != ',') return 0;
    return 1;
}

int test_strtok_r(void) {
    char buf[] = "a,b,c,d";
    char *save;
    char *tok = strtok_r(buf, ",", &save);
    if (!tok || strcmp(tok, "a") != 0) return 0;
    tok = strtok_r(NULL, ",", &save);
    if (!tok || strcmp(tok, "b") != 0) return 0;
    tok = strtok_r(NULL, ",", &save);
    if (!tok || strcmp(tok, "c") != 0) return 0;
    tok = strtok_r(NULL, ",", &save);
    if (!tok || strcmp(tok, "d") != 0) return 0;
    tok = strtok_r(NULL, ",", &save);
    if (tok) return 0;
    return 1;
}

int main(void) {
    int failed = 0;
    struct { const char *name; int (*fn)(void); } tests[] = {
        {"sprintf_basic",    test_sprintf_basic},
        {"snprintf_trunc",   test_snprintf_trunc},
        {"sscanf_int",       test_sscanf_int},
        {"sscanf_hex",       test_sscanf_hex},
        {"sscanf_string",    test_sscanf_string},
        {"strerror",         test_strerror},
        {"strftime",         test_strftime},
        {"qsort",            test_qsort},
        {"bsearch",          test_bsearch},
        {"strstr_strpbrk",   test_strstr_strpbrk},
        {"strtok_r",         test_strtok_r},
    };
    for (size_t i = 0; i < sizeof(tests)/sizeof(tests[0]); i++) {
        int r = tests[i].fn();
        printf("%-20s %s\n", tests[i].name, r ? "PASS" : "FAIL");
        if (!r) failed++;
    }
    if (failed) {
        printf("\n%d test(s) FAILED\n", failed);
        return 1;
    }
    printf("\nALL PASS\n");
    return 0;
}
