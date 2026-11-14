// EXPECT: octal-ok
#include <stdio.h>
int main(void) {
    const char *esc = "\033[";
    const char *nul = "a\0b";
    if (esc[0] != 27 || esc[1] != '[' || esc[2] != 0) return 1;
    if (nul[0] != 'a' || nul[1] != 0 || nul[2] != 'b') return 2;
    if ('\0' != 0 || '\101' != 'A' || "\1011"[1] != '1') return 3;
    puts("octal-ok");
    return 0;
}
