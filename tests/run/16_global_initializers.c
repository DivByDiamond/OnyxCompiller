// EXPECT: entry=42 bits=10 offset=8
#include <stdio.h>
#include <stddef.h>
static int answer(void) { return 42; }
struct entry { const char *name; int (*call)(void); };
static struct entry functions[] = {{"entry", answer}, {0, 0}};
static char braced_string[] = {"ab" "c"};
static unsigned char flags[] = {(1 << 3) | 2};
struct pair { char tag; long value; };
static int offset = offsetof(struct pair, value);
int main(void) {
    if (braced_string[1] != 'b' || sizeof(braced_string) != 4) return 1;
    printf("%s=%d bits=%d offset=%d\n", functions[0].name,
           functions[0].call(), flags[0], offset);
    return 0;
}
