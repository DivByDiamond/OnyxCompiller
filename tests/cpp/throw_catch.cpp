// throw + catch by class reference; the statement after throw is skipped.
// EXPECT: try
// EXPECT: caught 7
// EXPECT: done
#include <stdio.h>

class MyErr {
    int code;
public:
    MyErr(int c) { code = c; }
    int get() { return code; }
};

int main() {
    try {
        printf("try\n");
        throw MyErr(7);
        printf("unreachable\n");
    } catch (MyErr& e) {
        printf("caught %d\n", e.get());
    }
    printf("done\n");
    return 0;
}
