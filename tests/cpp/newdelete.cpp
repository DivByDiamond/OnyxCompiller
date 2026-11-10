// new / delete: malloc + constructor, destructor + free.
// EXPECT: 42
// EXPECT: freed
#include <stdio.h>

class Box {
    int v;
public:
    Box(int x) { v = x; }
    int get() { return v; }
    ~Box() { printf("freed\n"); }
};

int main() {
    Box *b = new Box(42);
    printf("%d\n", b->get());
    delete b;
    return 0;
}
