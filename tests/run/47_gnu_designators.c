// EXPECT: ok
#include <stdio.h>
struct P { int x, y; };
struct Q { int a[6]; struct P p; };
int ga[8] = { [1 ... 3] = 7, [6] = 1, [4 ... 5] = 2 };
int gi[] = { [2 ... 4] = 5, 9 };
struct P gp = { x: 3, y: 4 };
struct Q gq = { .a[1 ... 2] = 8, p: { y: 1 } };
struct P gps[4] = { [1 ... 2] = { .x = 1, .y = 2 } };
int main(void) {
    int la[6] = { [0 ... 2] = 3, [4 ... 5] = 6 };
    int li[] = { [1 ... 2] = 4, 5 };
    struct P lp = { y: 2, x: 1 };
    struct Q lq = { .a[3 ... 4] = 1 };
    if (ga[0] || ga[1] != 7 || ga[3] != 7 || ga[4] != 2 || ga[5] != 2 || ga[6] != 1 || ga[7]) return 1;
    if (sizeof(gi) != 6 * sizeof(int) || gi[1] || gi[2] != 5 || gi[4] != 5 || gi[5] != 9) return 2;
    if (gp.x != 3 || gp.y != 4) return 3;
    if (gq.a[0] || gq.a[1] != 8 || gq.a[2] != 8 || gq.a[3] || gq.p.x || gq.p.y != 1) return 4;
    if (gps[0].y || gps[1].y != 2 || gps[2].x != 1 || gps[3].x) return 5;
    if (la[0] != 3 || la[2] != 3 || la[3] || la[4] != 6 || la[5] != 6) return 6;
    if (sizeof(li) != 4 * sizeof(int) || li[0] || li[1] != 4 || li[2] != 4 || li[3] != 5) return 7;
    if (lp.x != 1 || lp.y != 2) return 8;
    if (lq.a[2] || lq.a[3] != 1 || lq.a[4] != 1 || lq.a[5]) return 9;
    puts("ok");
    return 0;
}
