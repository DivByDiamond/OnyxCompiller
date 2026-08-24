// EXPECT: sq=25 max=9 cat=77 LOG: v=1
#include <stdio.h>
#define SQUARE(x) ((x) * (x))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CAT(a, b) a##b
#define LOG(...) printf("LOG: " __VA_ARGS__)
int main(void) {
    int cat_var = 77;
    printf("sq=%d max=%d cat=%d ", SQUARE(5), MAX(3, 9), cat_var);
    LOG("v=%d\n", 1);
    return 0;
}
