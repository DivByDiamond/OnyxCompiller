// EXPECT: ok
#include <stdio.h>
struct P { int x, y; };
struct R { const char *name; struct P *p; int *a; };
int *ia = (int[]){ 1, 2, 3 };
struct P *pp = &(struct P){ .y = 5, .x = 2 };
const char **names = (const char *[]){ "a", "bc", 0 };
struct R r = { "r", &(struct P){ 7, 8 }, (int[]){ [2] = 9, 1 } };
struct R rs[2] = { { .p = &(struct P){ 1, 1 } }, { .a = (int[]){ 4, 5 } } };
int after = 77;
int counter(void) { static struct P *sp = &(struct P){ 3, 4 }; sp->x++; return sp->x; }
int main(void) {
    if (ia[0] != 1 || ia[2] != 3) return 1;
    if (pp->x != 2 || pp->y != 5) return 2;
    if (names[1][1] != 'c' || names[2]) return 3;
    if (r.name[0] != 'r' || r.p->y != 8 || r.a[2] != 9 || r.a[3] != 1) return 4;
    if (rs[0].p->x != 1 || rs[1].a[1] != 5 || rs[0].a || rs[1].p) return 5;
    if (after != 77) return 6;
    pp->x = 10; if (ia == (int *)pp) return 7;
    if (counter() != 4 || counter() != 5) return 8;
    puts("ok");
    return 0;
}
