// EXPECT: ok
#include <stdio.h>
struct S { int a; int b[3]; long c; };
int arr[5] = {0, 1, 2, 3, 4};
struct S s = {1, {2, 3, 4}, 5};
struct S ss[2];
static int *p1 = &arr[2];
static int *p2 = arr + 3;
static int *p3 = &s.b[1];
static long *p4 = &s.c;
static int *tbl[] = { &arr[1], &ss[1].b[2], arr, &arr[4] };
static int *q[2][2] = { { &arr[1], &arr[2] }, [1][1] = &s.a };
int main(void) {
    if (p1 != &arr[2] || *p1 != 2) return 1;
    if (p2 != &arr[3] || *p2 != 3) return 2;
    if (p3 != &s.b[1] || *p3 != 3) return 3;
    if (p4 != &s.c || *p4 != 5) return 4;
    if (tbl[0] != &arr[1] || tbl[2] != arr || tbl[3] != &arr[4]) return 5;
    if (q[0][1] != &arr[2] || q[1][1] != &s.a || q[1][0]) return 6;
    puts("ok");
    return 0;
}
