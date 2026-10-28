// EXPECT: postfix-ok
#include <stdio.h>
struct object { char *name; long value; };
static struct object item = {"abc", 42};
static struct object *get(void) { return &item; }
static char *text(void) { return item.name; }
static int index(void) { return 1; }
int main(void) {
    if ("abc"[1] != 'b') return 1;
    if (get()->value != 42) return 2;
    if (text()[index()] != 'b') return 3;
    if (get()->name[index()] != 'b') return 4;
    printf("postfix-ok\n"); return 0;
}
