// EXPECT: string-arrays-ok
#include <stdio.h>
int main(void) {
    if (sizeof("abc") != 4 || sizeof("\x1bLua") != 5) return 1;
    if ("abc"[1] != 'b') return 2;
    printf("string-arrays-ok\n"); return 0;
}
