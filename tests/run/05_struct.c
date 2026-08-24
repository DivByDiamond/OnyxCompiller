// EXPECT: point(3,4) len=25
#include <stdio.h>
typedef struct { int x, y; } point_t;
static int len2(point_t p) { return p.x * p.x + p.y * p.y; }
int main(void) {
    point_t p = {3, 4};
    printf("point(%d,%d) len=%d\n", p.x, p.y, len2(p));
    return 0;
}
