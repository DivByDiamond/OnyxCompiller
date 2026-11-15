// EXPECT: ok
#include <stdio.h>
struct P { int x; long y; };
int cnt;
int bump(void) { return ++cnt; }
int main(void) {
    int lg[5]; struct P p; struct P *pp = &p; char c; short sh;
    int a = 0;
    if (sizeof lg != 20) return 1;
    if (sizeof p != 16 || sizeof *pp != 16 || sizeof pp->y != 8) return 2;
    if (sizeof lg[0] != 4 || sizeof c != 1 || sizeof (sh) != 2) return 3;
    if (sizeof(int) != 4 || sizeof (struct P) != 16) return 4;
    if (sizeof a++ != 4 || a != 0) return 5;
    if (sizeof bump() != 4 || cnt != 0) return 6;
    if (sizeof "abc" != 4 || sizeof -c != 4) return 7;
    if (sizeof (lg)[1] != 4) return 8;
    puts("ok");
    return 0;
}
