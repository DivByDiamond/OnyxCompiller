// RAII: constructors and destructors run in the expected order.
// EXPECT: ctor 1
// EXPECT: ctor 2
// EXPECT: dtor 2
// EXPECT: ctor 3
// EXPECT: end
// EXPECT: dtor 3
// EXPECT: dtor 1
#include <stdio.h>

class Tracer {
    int id;
public:
    Tracer(int i) {
        id = i;
        printf("ctor %d\n", id);
    }
    ~Tracer() { printf("dtor %d\n", id); }
};

int main() {
    Tracer a(1);
    { Tracer b(2); }
    Tracer c(3);
    printf("end\n");
    return 0;
}
