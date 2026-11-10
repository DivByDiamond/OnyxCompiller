// Method overloading resolved by argument count.
// EXPECT: 3 7
#include <stdio.h>

class Adder {
public:
    int add(int a) { return a; }
    int add(int a, int b) { return a + b; }
};

int main() {
    Adder x;
    printf("%d %d\n", x.add(3), x.add(3, 4));
    return 0;
}
