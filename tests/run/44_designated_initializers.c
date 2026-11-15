// EXPECT: ok
#include <stdio.h>
#include <string.h>

struct P { int x, y; };
struct Q { char tag[8]; struct P p; int *ptr; const char *name; short arr[4]; };
union U { int i; char c[4]; };
static int target = 42;
static int tbl[3] = {10, 20, 30};

/* Globals: out-of-order, nested, pointers, strings, overwrite, incomplete. */
struct P g1 = { .y = 5, .x = 2 };
int ga[6] = { [4] = 7, 8, [1] = 3 };
int gi[] = { 1, [5] = 9, 10 };
struct Q gq = { .name = "hi", .ptr = &target, .p.y = 4, .arr[2] = 6, .tag = "abc" };
struct Q gqa[2] = { [1] = { .name = "second", .p = { .x = 1 } }, [0].ptr = tbl };
union U gu = { .c = {1, 2, 3, 4} };
struct P gover = { .x = 1, .y = 2, .x = 3 };
int g2d[2][3] = { [1][2] = 5, [0] = {1, 2} };
const char *names[] = { [2] = "two", [0] = "zero" };

static int sum(struct P *p) { return p->x + p->y; }
static int add3(int a, struct P p, int b) { return a + p.x + p.y + b; }

int main(void) {
    /* Locals */
    struct P a = { .y = 5, .x = 2 };
    int arr[5] = { [3] = 7, 1 };
    struct Q q = { .p.x = 1, .arr = {1, 2}, .name = "n", .tag = "t" };
    int lg[] = { [2] = 1, [0] = 4, 5, 6, 7 };
    union U u = { .c = {9} };
    int m2[2][2] = { [1] = {3, 4}, [0][1] = 2 };
    if (a.x != 2 || a.y != 5) return 1;
    if (arr[3] != 7 || arr[4] != 1 || arr[0] != 0 || arr[1] != 0) return 2;
    if (q.p.x != 1 || q.p.y != 0 || q.arr[1] != 2 || q.arr[3] != 0 || strcmp(q.name, "n")) return 3;
    if (sizeof(lg) != 4 * sizeof(int) || lg[0] != 4 || lg[1] != 5 || lg[2] != 6 || lg[3] != 7) return 4;
    if (u.c[0] != 9) return 5;
    if (m2[0][0] != 0 || m2[0][1] != 2 || m2[1][0] != 3 || m2[1][1] != 4) return 6;
    /* Compound literals */
    struct P *p = &(struct P){ 3, 4 };
    int *ip = (int[]){1, 2, 3};
    struct P b = (struct P){ .x = 1, .y = 9 };
    if (sum(p) != 7 || ip[2] != 3 || b.y != 9 || b.x != 1) return 7;
    if (1 + sum(&(struct P){ .y = 10 }) != 11) return 8;
    if (add3(1, (struct P){ 2, 3 }, 4) != 10) return 9;
    if ((struct P){ .x = 6, .y = 1 }.x != 6) return 10;
    if (sizeof((int[]){1, 2, 3, 4}) != 16) return 11;
    int total = 0;
    for (int i = 0; i < 3; i++) {
        struct P t = (struct P){ .y = i };
        total += t.x + t.y;
        int *r = (int[]){ i, i * 2 };
        total += r[1];
    }
    if (total != 3 + 6) return 12;
    /* Globals */
    if (g1.x != 2 || g1.y != 5) return 20;
    if (ga[0] || ga[1] != 3 || ga[4] != 7 || ga[5] != 8) return 21;
    if (sizeof(gi) != 7 * sizeof(int) || gi[0] != 1 || gi[5] != 9 || gi[6] != 10 || gi[3]) return 22;
    if (strcmp(gq.name, "hi") || *gq.ptr != 42 || gq.p.y != 4 || gq.p.x || gq.arr[2] != 6 || strcmp(gq.tag, "abc")) return 23;
    if (strcmp(gqa[1].name, "second") || gqa[1].p.x != 1 || gqa[0].ptr != tbl || gqa[0].name || gqa[1].ptr) return 24;
    if (gu.c[0] != 1 || gu.c[3] != 4) return 25;
    if (gover.x != 3 || gover.y != 2) return 26;
    if (g2d[1][2] != 5 || g2d[0][0] != 1 || g2d[0][1] != 2 || g2d[1][0]) return 27;
    if (sizeof(names) != 3 * sizeof(char *) || strcmp(names[0], "zero") || names[1] || strcmp(names[2], "two")) return 28;
    puts("ok");
    return 0;
}
