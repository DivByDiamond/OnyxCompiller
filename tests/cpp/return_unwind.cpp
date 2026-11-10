// Early return still runs live destructors; the return value survives.
// EXPECT: dtor
// EXPECT: 7
#include <stdio.h>

class Guard {
public:
    ~Guard() { printf("dtor\n"); }
};

int f() {
    Guard g;
    return 7;
}

int main() {
    printf("%d\n", f());
    return 0;
}
