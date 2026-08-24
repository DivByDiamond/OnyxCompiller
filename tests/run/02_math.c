// EXPECT: sqrt=1.4142 pow=1024.0 sin=0.8415
// EXPECT: floor=2.0
#include <stdio.h>
#include <math.h>
int main(void) {
    printf("sqrt=%.4f pow=%.1f sin=%.4f\n", sqrt(2.0), pow(2.0, 10.0), sin(1.0));
    printf("floor=%.1f\n", floor(2.7));
    return 0;
}
