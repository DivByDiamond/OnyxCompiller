// Class basics: data members, constructor with arguments, inline methods.
// EXPECT: 7 3
#include <stdio.h>

class Point {
    int x;
    int y;
public:
    Point(int a, int b) {
        x = a;
        y = b;
    }
    int sum() { return x + y; }
    int getx() { return x; }
};

int main() {
    Point p(3, 4);
    printf("%d %d\n", p.sum(), p.getx());
    return 0;
}
