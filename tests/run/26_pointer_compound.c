// EXPECT: pointer-compound-ok
#include <stdio.h>
int main(void) {
    long items[8];
    long *pointer = items;
    pointer += 3;
    if (pointer != &items[3]) return 1;
    pointer -= 2;
    if (pointer != &items[1]) return 2;
    if (items + 2 != &items[2] || 2 + items != &items[2]) return 3;
    if (pointer - items != 1 || items - pointer != -1) return 4;
    printf("pointer-compound-ok\n");
    return 0;
}
