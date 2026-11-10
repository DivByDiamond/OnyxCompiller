// EXPECT: 42 42 40 2
#include <stdio.h>

extern int sep_add(int a, int b);
extern int sep_double(int x);
extern int sep_global;
extern int sep_bss;
extern int sep_seed_get(void);

int main(void) {
    sep_bss = 2;
    printf("%d %d %d %d\n",
           sep_add(sep_global, sep_bss),
           sep_double(21),
           sep_global,
           sep_seed_get());
    return 0;
}
