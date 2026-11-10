// Single inheritance: flattened base fields, base pointer upcast.
// EXPECT: 40 42 40
#include <stdio.h>

class Base {
public:
    int b;
    int base_val() { return b; }
};

class Derived : public Base {
public:
    int d;
    int total() { return b + d; }
};

int main() {
    Derived x;
    x.b = 40;
    x.d = 2;
    Base *p = &x;
    printf("%d %d %d\n", p->b, x.total(), x.base_val());
    return 0;
}
