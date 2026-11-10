// Virtual dispatch through a base-class pointer.
// EXPECT: 4 2
#include <stdio.h>

class Animal {
public:
    virtual int legs() { return 4; }
};

class Bird : public Animal {
public:
    int legs() { return 2; }
};

int main() {
    Animal a;
    Bird b;
    Animal *p = &a;
    Animal *q = &b;
    printf("%d %d\n", p->legs(), q->legs());
    return 0;
}
