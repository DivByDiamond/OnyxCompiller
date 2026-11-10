// Out-of-line member definitions plus the implicit default constructor.
// EXPECT: 11
#include <stdio.h>

class Counter {
    int n;
public:
    Counter();
    int value();
    void bump();
};

Counter::Counter() { n = 10; }
int Counter::value() { return n; }
void Counter::bump() { n = n + 1; }

int main() {
    Counter c;
    c.bump();
    printf("%d\n", c.value());
    return 0;
}
