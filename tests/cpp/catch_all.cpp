// Nested try, catch(...) and rethrow; the outer clause catches by value.
// EXPECT: inner all
// EXPECT: outer 42
// EXPECT: done
#include <stdio.h>

int main() {
    try {
        try {
            throw 42;
        } catch (...) {
            printf("inner all\n");
            throw;
        }
    } catch (int v) {
        printf("outer %d\n", v);
    }
    printf("done\n");
    return 0;
}
