// Function template with explicit instantiation (definition before use).
// EXPECT: 9 10
#include <stdio.h>

template<class T> T maxv(T a, T b) { return a > b ? a : b; }

int main() {
    printf("%d %d\n", maxv<int>(3, 9), maxv<int>(10, 2));
    return 0;
}
