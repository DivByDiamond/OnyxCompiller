// EXPECT: signed-ok
#include <stdio.h>
static long negative(long value) { return value >> 32; }
static int narrowed(long value) { return value; }
int main(void) {
    if (negative(-4294967296L) != -1) return 1;
    if (narrowed(4294967295UL) != -1) return 2;
    printf("signed-ok\n"); return 0;
}
