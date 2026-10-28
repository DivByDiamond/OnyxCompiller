// EXPECT: environment-ok
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
int main(void) {
    if (setenv("ONYX_TEST_ENV", "one", 1)) return 1;
    if (setenv("ONYX_TEST_ENV", "two", 0)) return 2;
    if (strcmp(getenv("ONYX_TEST_ENV"), "one")) return 3;
    if (setenv("ONYX_TEST_ENV", "two", 1)) return 4;
    if (strcmp(getenv("ONYX_TEST_ENV"), "two")) return 5;
    if (setenv("bad=name", "x", 1) != -1 || errno != EINVAL) return 6;
    if (setenv("", "x", 1) != -1 || errno != EINVAL) return 7;
    if (unsetenv("ONYX_TEST_ENV") || getenv("ONYX_TEST_ENV")) return 8;
    if (unsetenv("ONYX_TEST_ENV")) return 9;
    printf("environment-ok\n");
    return 0;
}
