// EXPECT: conditional-conversions-ok
#include <stdio.h>
struct pair { int x,y; };
static int calls;
static double select(int x) { return x ? 1 : 2.5; }
static double reverse(int x) { return x ? 3.5 : 2; }
static int tick(void) { ++calls; return 7; }
int main(void) {
    int x=1;
    if (select(x)!=1.0 || select(0)!=2.5) return 1;
    if (reverse(x)!=3.5 || reverse(0)!=2.0) return 2;
    if ((x ? -1 : 1U)!=4294967295U) return 3;
    if ((0.5 ? 42 : 0)!=42 || (0.0 ? 0 : 42)!=42) return 4;
    if ((x ? 4 : tick())!=4 || calls) return 5;
    if ((0 ? tick() : 9)!=9 || calls) return 6;
    double nested=x ? (0 ? 1 : 4.5) : 8;
    if (nested!=4.5) return 7;
    char *s=x ? "yes" : (char *)0;
    if (!s || s[0]!='y') return 8;
    struct pair a={20,22}, b={1,2};
    struct pair c=x ? a : b;
    if (c.x+c.y!=42) return 9;
    printf("conditional-conversions-ok\n");
    return 0;
}
