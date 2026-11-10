// References: `T&` parameters auto-dereference at the use site.
// EXPECT: 6
#include <stdio.h>

void inc(int& r) { r = r + 1; }

int f() {
    int x = 5;
    inc(x);
    return x;
}

int main() {
    printf("%d\n", f());
    return 0;
}
