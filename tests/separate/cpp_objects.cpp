// C++ objects through the separate-compilation pipeline (`-c` → onyx-ld).
// Covers both vtable shapes:
//   * Pair has no virtual methods: cpp_class_open reserves __vt_Pair, but
//     it is never filled — the object must NOT export it as UNDEF.
//   * Base/Derived are polymorphic: their vtables live in .data and are
//     referenced through R_ONYO_64/HI20 relocations.
// EXPECT: 3 2
#include <stdio.h>

class Pair {
    int a;
    int b;
public:
    Pair(int x, int y) {
        a = x;
        b = y;
    }
    int sum() { return a + b; }
};

class Base {
public:
    virtual int value() { return 1; }
};

class Derived : public Base {
public:
    int value() { return 2; }
};

int main() {
    Pair p(1, 2);
    Base b;
    Derived d;
    Base *dp = &d;
    printf("%d %d\n", p.sum(), dp->value());
    return 0;
}
