// EXPECT: switch-ok
#include <stdio.h>
static int select(int value) {
    switch (value) {
    case (6 | (1 << 4)): return 22;
    default: return 99;
    case 6: return 6;
    }
}
static int fallthrough(int value) {
    int result = 0;
    switch (value) {
    case 1: result += 10;
    case 2: result += 20;
    case 3: result += 30; break;
    default: result = -1;
    }
    return result;
}
int main(void) {
    if (select(22) != 22 || select(6) != 6 || select(0) != 99) return 1;
    if (fallthrough(1) != 60 || fallthrough(2) != 50 || fallthrough(3) != 30) return 2;
    unsigned int max = 0xffffffffU;
    switch (max) { case -1: break; default: return 3; }
    int minus = -1;
    switch (minus) { case 0xffffffffU: break; default: return 4; }
    printf("switch-ok\n"); return 0;
}
