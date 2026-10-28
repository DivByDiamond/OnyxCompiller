// EXPECT: large-fp-frame-ok
#include <stdio.h>
static double pass(double x) { return x; }
int main(void) {
    char sentinel[4096];
    for (int i=0; i<4096; i++) sentinel[i] = 37;
    double x = pass(3.25);
    if (x != 3.25) return 1;
    for (int i=0; i<4096; i++) if (sentinel[i] != 37) return 2;
    printf("large-fp-frame-ok\n"); return 0;
}
