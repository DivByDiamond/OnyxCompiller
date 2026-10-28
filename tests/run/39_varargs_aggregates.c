// EXPECT: aggregate-varargs-ok
#include <stdio.h>
#include <stdarg.h>
struct cursor { va_list source, copy; };
struct small { char x; };
static int inspect(struct cursor *a, struct cursor *b, int count, ...) {
    va_start(a->source,count);
    va_copy(b->copy,a->source);
    struct small first=va_arg(a->source,struct small);
    struct small second=va_arg(a->source,struct small);
    struct small again=va_arg(b->copy,struct small);
    if (first.x != again.x) return -1;
    va_end(a->source); va_end(b->copy);
    return first.x+second.x;
}
int main(void) {
    struct cursor a={0,0}, b={0,0};
    struct small first={20}, second={22};
    if (inspect(&a,&b,2,first,second) != 42) return 1;
    printf("aggregate-varargs-ok\n"); return 0;
}
