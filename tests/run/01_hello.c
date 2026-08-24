// EXPECT: Hello, OnyxOS!
// EXPECT: Numbers: -42, 42, dead, ok, X
#include <stdio.h>
int main(void) {
    printf("Hello, OnyxOS!\n");
    printf("Numbers: %d, %u, %x, %s, %c\n", -42, 42u, 0xDEAD, "ok", 'X');
    return 0;
}
