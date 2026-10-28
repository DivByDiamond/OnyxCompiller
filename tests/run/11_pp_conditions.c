// EXPECT: pp-ok
#include <stdio.h>
#define PRESENT 9
#if !defined(PRESENT)
#error defined operand must not be expanded
#endif
#if 0
#if 0
#elif 1
#error inactive parent elif escaped
#endif
#if 0
#else
#error inactive parent else escaped
#endif
#endif
#if !(0 || 1) || (0 && 1)
#error logical parentheses failed
#endif
#if (1 << 4) != 16 || (32 >> 3) != 4 || (7 ^ 3) != 4
#error shifts or xor failed
#endif
#if (1 ? 2 : 3) != 2 || (0 ? 2 : 3) != 3 || 010 != 8
#error conditional or octal failed
#endif
#if !defined( PRESENT ) || !defined PRESENT
#error spaced defined failed
#endif
#if 0
#if UNKNOWN(1)
#error disabled condition was evaluated
#endif
#endif
#if !(0xffffffffffffffffULL > 0) || (-1 < 1U)
#error unsigned comparison failed
#endif
#if (0xffffffffffffffffULL >> 63) != 1 || (0xffffffffffffffffULL / 2) != 9223372036854775807ULL
#error unsigned arithmetic failed
#endif
#define defined_suffix 7
#if defined_suffix != 7 || ('a' != 97)
#error defined token boundary or character constant failed
#endif
int main(void) { printf("pp-ok\n"); return 0; }
