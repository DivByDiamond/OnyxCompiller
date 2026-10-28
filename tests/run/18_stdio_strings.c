// EXPECT: hello
// EXPECT: joined-line
// EXPECT: byte-block
// EXPECT: stdio-strings-ok
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
int main(void) {
    if (puts("hello") < 0) return 1;
    if (fputs("joined-", stdout) < 0 || fputs("line\n", stdout) < 0) return 2;
    if (fwrite("byte-block\n", 1, 11, stdout) != 11) return 3;
    if (fwrite("unused", 0, 7, stdout) != 0) return 4;
    if (fwrite("unused", 7, 0, stdout) != 0) return 5;
    if (fwrite("x", (size_t)-1, 2, stdout) != 0 || !ferror(stdout)) return 6;
    clearerr(stdout);
    if (fputs("must-not-write", stdin) >= 0 || !ferror(stdin) || errno != EBADF) return 7;
    clearerr(stdin);
    if (fwrite("x", 1, 1, stdin) != 0 || !ferror(stdin)) return 8;
    clearerr(stdin);
    if (fwrite("x", 1, 1, NULL) != 0 || errno != EFAULT) return 9;
    if (fputs("x", NULL) >= 0) return 10;
    if (puts("stdio-strings-ok") < 0) return 11;
    return 0;
}
