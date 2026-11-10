// Uncaught exception terminates with exit code 134.
// EXPECT: before
// EXPECT: terminate: uncaught exception
// EXPECT-EXIT: 134
#include <stdio.h>

int main() {
    printf("before\n");
    throw 5;
    return 0;
}
