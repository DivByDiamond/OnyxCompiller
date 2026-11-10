// Namespaces (flattened): unqualified and qualified references.
// EXPECT: 7 7
#include <stdio.h>

namespace math {
    int add(int a, int b) { return a + b; }
}

using namespace math;

int main() {
    printf("%d %d\n", add(3, 4), math::add(3, 4));
    return 0;
}
