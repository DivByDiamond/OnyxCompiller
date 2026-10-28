// EXPECT: allocator-ok
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
int main(void) {
    unsigned char *p = malloc(16);
    int i;
    if (!p) return 1;
    for (i = 0; i < 16; i++) p[i] = i;
    void *old = p;
    free(p);
    p = malloc(16);
    if (p != old) { printf("not-reused\n"); return 2; }
    for (i = 0; i < 16; i++) p[i] = i;
    p = realloc(p, 4096);
    if (!p) return 3;
    for (i = 0; i < 16; i++) if (p[i] != i) return 4;
    free(p);
    if (calloc((size_t)-1, 2) != NULL) return 5;
    p = calloc(32, 4);
    if (!p || ((uintptr_t)p & 15)) return 6;
    for (i = 0; i < 128; i++) if (p[i] != 0) return 7;
    for (i = 0; i < 128; i++) p[i] = i;
    unsigned char *guard = malloc(128);
    if (!guard) return 8;
    unsigned char *grown = realloc(p, 8192);
    if (!grown) return 9;
    for (i = 0; i < 128; i++) if (grown[i] != i) return 10;
    p = realloc(grown, 32);
    if (p != grown) return 11;
    for (i = 0; i < 32; i++) if (p[i] != i) return 12;
    if (realloc(p, (size_t)-1) != NULL) return 13;
    for (i = 0; i < 32; i++) if (p[i] != i) return 14;
    if (realloc(p, 0) != NULL) return 15;
    free(guard);
    free(NULL);
    p = realloc(NULL, 24);
    if (!p) return 16;
    free(p);
    p = calloc(0, 12);
    free(p);
    printf("allocator-ok\n");
    return 0;
}
