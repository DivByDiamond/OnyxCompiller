// EXPECT: ecall-ok
#include <onyxc.h>
#include "../../include/sys/syscall_numbers.h"
static long count(void) { _onyx_getpid(); return 9; }
int main(void) {
    long result = __ecall4(SYS_write, 1, (long)"ecall-ok\n", count());
    return result == 9 ? 0 : 1;
}
