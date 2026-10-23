/* Compile with --ring1 and run from the root shell. Drop to ring 2 while
 * retaining uid 0, so VFS permits reading ring-1 /bin/osh and the rejection
 * happens after loading. Repeated calls exhaust user pages if exec leaks.
 */
#include "../include/sys/syscall_numbers.h"

long __ecall1(long n);
long __ecall2(long n, long a);
long __ecall3(long n, long a, long b);
long __ecall4(long n, long a, long b, long c);

static void message(const char *text) {
    long length = 0;
    while (text[length]) length = length + 1;
    __ecall4(SYS_write, 1, (long)text, length);
}

int main(void) {
    const char *target = "/bin/osh";
    const char *args[2];
    int i;
    args[0] = target;
    args[1] = 0;
    if (__ecall1(SYS_getuid) != 0 || __ecall2(SYS_dropring, 2) != 0) {
        message("FAIL fixture requires root and --ring1\n");
        return 1;
    }
    for (i = 0; i < 256; i = i + 1) {
        if (__ecall3(SYS_exec, (long)target, (long)args) != -1) {
            message("FAIL legacy exec denial\n");
            return 1;
        }
        if (__ecall4(SYS_execve, (long)target, (long)args, 0) != -1) {
            message("FAIL execve denial\n");
            return 1;
        }
    }
    message("EXEC_DENIAL_512_OK\n");
    return 0;
}
