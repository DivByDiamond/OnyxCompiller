// ARGS: hello world
// EXPECT: argc=3 arg1=hello arg2=world
#include <stdio.h>
int main(int argc, char **argv) {
    printf("argc=%d arg1=%s arg2=%s\n", argc, argv[1], argv[2]);
    return 0;
}
