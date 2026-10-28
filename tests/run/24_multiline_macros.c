// EXPECT: 10 21 16
// EXPECT: (,) 9
// EXPECT: hello world 5 41
// EXPECT: 5 VERSION 17 23
// EXPECT: tagVERSION tag5 "5" 3
#include <stdio.h>
#define SUM(a, b) ((a) + (b))
#define TWICE(x) ((x) + (x))
#define ID(x) x
#define ALIAS SUM
#define STRING(x) #x
#define JOIN(a, b) a##b
#define VERSION 5
#define RAWSTR(x) #x
#define STR(x) RAWSTR(x)
#define RAWPASTE(a, b) a ## b
#define PASTE(a, b) RAWPASTE(a, b)
#define COMMA 1, 2
#define PAIR(a, b) SUM(a, b)
#define WRAP(x) PAIR(x)
#if 1 + 2 * 3 == 7
#define ENABLED 1
#else
#error bad preprocessor condition
#endif
int main(void) {
    int value = SUM(1,
        SUM(2, /* unmatched punctuation in comments: ), */
            7));
    int before_paren = SUM
        (10,
         11);
    int nested = TWICE(
        SUM(3,
            5));
    int alias = ALIAS
        (4,
         5);
    const char *text = ID(
        "(,)"
    );
    int JOIN(test,
             number) = ENABLED;
    printf("%d %d %d\n", value, before_paren, nested);
    printf("%s %d\n", text, alias * testnumber);
    printf("%s %d %d\n", STRING(hello
             world), SUM((1, 2),
                         3), ID(
                             ')'));
    {
        int tagVERSION = 17, tag5 = 23;
        printf("%s %s %d %d\n", STR(VERSION), RAWSTR(VERSION),
               RAWPASTE(tag, VERSION), PASTE(tag, VERSION));
        printf("%s %s %s %d\n", STR(RAWPASTE(tag, VERSION)),
               STR(PASTE(tag, VERSION)), STR(STR(VERSION)), WRAP(COMMA));
    }
    return 0;
}
