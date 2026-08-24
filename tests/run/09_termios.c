// EXPECT: raw-on echo=0 canon=0
// EXPECT: raw-off echo=1 canon=1
#include <stdio.h>
#include <termios.h>
int main(void) {
    struct termios tio;
    tcgetattr(0, &tio);
    cfmakeraw(&tio);
    printf("raw-on echo=%d canon=%d\n", tio.c_lflag & ECHO ? 1 : 0, tio.c_lflag & ICANON ? 1 : 0);
    cfsetsane(&tio);
    printf("raw-off echo=%d canon=%d\n", tio.c_lflag & ECHO ? 1 : 0, tio.c_lflag & ICANON ? 1 : 0);
    return 0;
}
