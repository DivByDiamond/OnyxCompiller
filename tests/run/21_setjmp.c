// EXPECT: setjmp-ok
#include <stdio.h>
#include <setjmp.h>

static jmp_buf outer;
static jmp_buf inner;
static int count;

static void jump_deep(int depth, int value) {
    volatile double floating = 7.25;
    volatile int padding[16];
    padding[0] = depth;
    if (depth) jump_deep(depth - 1, value);
    if (floating != 7.25 || padding[0] != depth) longjmp(outer, 99);
    longjmp(inner, value);
}

static void nested(void) {
    volatile double kept = 3.5;
    volatile int stack[8];
    stack[3] = 42;
    int value;
    switch (setjmp(inner)) {
        case 0: jump_deep(3, 0); return;
        case 1: value = 1; break;
        case 2: value = 2; break;
        case 3: value = 3; break;
        case 4: value = 4; break;
        default: longjmp(outer, 99); return;
    }
    if (value != count + 1 || kept != 3.5 || stack[3] != 42) longjmp(outer, 99);
    count++;
    if (count < 4) jump_deep(2, count + 1);
    /* The second argument has C's int parameter conversion before zero
     * normalization, even when the intrinsic receives a wider operand. */
    switch (setjmp(inner)) {
        case 0: longjmp(inner, 0x100000000UL); return;
        case 1: break;
        default: longjmp(outer, 99); return;
    }
    longjmp(outer, 17);
}

int main(void) {
    volatile double kept = -2.75;
    volatile int marker = 123;
    switch (setjmp(outer)) {
        case 0: nested(); return 1;
        case 17: break;
        default: return 1;
    }
    if (count != 4 || kept != -2.75 || marker != 123) return 1;
    printf("setjmp-ok\n");
    return 0;
}
