// EXPECT: aggregate-return-ok
#include <stdio.h>
struct value { long a, b, c, d, e, f; };
static struct value make(long x) { struct value v={x,2,3,4,5,6}; return v; }
static struct value add(struct value v) { v.a+=10; v.f+=20; return v; }
static struct value choose(int n, struct value a, struct value b) { return n ? a : b; }
int main(void) {
    struct value v = make(7);
    if (v.a != 7 || v.f != 6) return 1;
    v = add(make(8));
    if (v.a != 18 || v.f != 26) return 2;
    v = choose(0, make(1), make(9));
    if (v.a != 9 || v.f != 6) return 3;
    printf("aggregate-return-ok\n"); return 0;
}
