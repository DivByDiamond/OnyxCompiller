// EXPECT: len=5 cmp=0 sub=World
#include <stdio.h>
#include <string.h>
int main(void) {
    char a[16] = "Hello";
    strcat(a, "World");
    printf("len=%d cmp=%d sub=%s\n", (int)strlen("Hello"), strcmp("abc", "abc"), &a[5]);
    return 0;
}
