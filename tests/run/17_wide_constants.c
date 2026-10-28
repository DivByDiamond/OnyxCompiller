// EXPECT: 7ff0000000000000 7fffffffffffffff 8000000000000000
// EXPECT: ffffffffffffffff 80045430 800fffffffffffff
// EXPECT: 123456789abcdef0 fffffffffffff800
#include <stdio.h>
int main(void) {
    printf("%lx %lx %lx\n", 0x7ff0000000000000UL,
           0x7fffffffffffffffUL, 0x8000000000000000UL);
    printf("%lx %lx %lx\n", 0xffffffffffffffffUL,
           0x80045430UL, (1UL << 63) | ((1UL << 52) - 1));
    printf("%lx %lx\n", 0x123456789abcdef0UL, (unsigned long)-2048L);
    return 0;
}
