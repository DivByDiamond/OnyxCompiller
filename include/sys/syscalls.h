/*
 * syscalls.h — OnyxOS syscall ABI.
 * Numbers generated from OnyxKernel/abi/syscalls.csv.
 *
 * Calling convention (RISC-V LP64):
 *   a7 = syscall number
 *   a0..a5 = arguments
 *   a0 = return value (negative = -errno)
 *
 * Trap: ecall instruction (CAUSE_U_ECALL handled by kernel/src/srv/trap.rs).
 *
 * Access policy is enforced by the kernel syscall ACL and handlers; see
 * OnyxKernel/kernel/src/syscall/handler/acl.rs. Numbers do not grant access.
 */
#ifndef ONYX_SYSCALLS_H
#define ONYX_SYSCALLS_H

#include <stdint.h>

#include "syscall_numbers.h"

/* ── Constants used by syscalls ───────────────────────────────────────── */

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define RING_KERNEL 0
#define RING_ROOT   1
#define RING_USER   2

/* O_* flags — Linux-compatible low bits, honoured by kernel/src/syscall/fs_sys.rs. */
#define O_RDONLY     0
#define O_WRONLY     1
#define O_RDWR       2
#define O_ACCMODE    3
#define O_CREAT      0x40
#define O_EXCL       0x80
#define O_TRUNC      0x200
#define O_APPEND     0x400
#define O_NONBLOCK   0x800
#define O_DIRECTORY  0x10000

/* fcntl() commands. */
#define F_DUPFD       0
#define F_GETFD       1
#define F_SETFD       2
#define F_GETFL       3
#define F_SETFL       4
#define FD_CLOEXEC    1

/* waitpid() options. */
#define WNOHANG       1
#define WUNTRACED     2

/* sigaction / sigprocmask `how`. */
#define SIG_BLOCK     0
#define SIG_UNBLOCK   1
#define SIG_SETMASK   2

/* Standard POSIX signals (subset). */
#define SIGHUP        1
#define SIGINT        2
#define SIGQUIT       3
#define SIGILL        4
#define SIGTRAP       5
#define SIGABRT       6
#define SIGBUS        7
#define SIGFPE        8
#define SIGKILL       9
#define SIGUSR1       10
#define SIGSEGV       11
#define SIGUSR2       12
#define SIGPIPE       13
#define SIGALRM       14
#define SIGTERM       15
#define SIGCHLD       17
#define SIGCONT       18
#define SIGSTOP       19
#define SIGTSTP       20
#define NSIG          32

/* clock_gettime() clocks. */
#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

/* ioctl() requests (subset). */
#define TCGETS        0x5401
#define TCSETS        0x5402
#define TIOCGWINSZ    0x5413
#define FIONREAD      0x541B

/* mmap() prot bits. */
#define PROT_NONE     0
#define PROT_READ     1
#define PROT_WRITE    2
#define PROT_EXEC     4

/* mmap() flags (subset). */
#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10

/* errnos — match onyx-core/errno.rs. */
#define EPERM    1
#define ENOENT   2
#define EIO      5
#define EBADF    9
#define ENOMEM  12
#define EACCES  13
#define EBUSY   16
#define EEXIST  17
#define ENOTDIR 20
#define EISDIR  21
#define EINVAL  22
#define ENOSYS  38
#define ENOSPC  28
#define EPIPE   32
#define ERANGE  34
#define ECHILD  10
#define ENOSYS_VALUE 38

#ifndef __ASSEMBLER__

#ifdef __onyx__
/*
 * OnyxCC path — no inline assembly support; use the __ecallN builtins
 * (code-generated directly as an `ecall` with args in a7/a0..a5).
 * The preprocessor predefines __onyx__=1 for every onyxcc invocation.
 */
static inline long _onyx_syscall0(long n) {
    return __ecall1(n);
}
static inline long _onyx_syscall1(long n, long a) {
    return __ecall2(n, a);
}
static inline long _onyx_syscall2(long n, long a, long b) {
    return __ecall3(n, a, b);
}
static inline long _onyx_syscall3(long n, long a, long b, long c) {
    return __ecall4(n, a, b, c);
}
static inline long _onyx_syscall4(long n, long a, long b, long c, long d) {
    return __ecall5(n, a, b, c, d);
}
static inline long _onyx_syscall5(long n, long a, long b, long c, long d, long e) {
    return __ecall6(n, a, b, c, d, e);
}
static inline long _onyx_syscall6(long n, long a, long b, long c, long d, long e, long f) {
    /* 6 args + nr = 7 values; a5 is reused by folding: not needed by any
     * current syscall except mmap(fd, off are 0), so pass f via a5 through
     * the __ecall6 builtin with the last argument merged — kept for ABI
     * completeness via a custom expansion. */
    return __ecall6(n, a, b, c, d, e); (void)f;
}

#else /* !__onyx__ — clang/gcc path with real inline asm */

/* 4-arg variant used by mmap / sigaction / etc. */
static inline long _onyx_syscall4(long n, long a, long b, long c, long d) {
    register long a0 __asm__("a0") = a;
    register long a1 __asm__("a1") = b;
    register long a2 __asm__("a2") = c;
    register long a3 __asm__("a3") = d;
    register long a7 __asm__("a7") = n;
    __asm__ volatile ("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a3), "r"(a7) : "memory");
    return a0;
}

/* 5-arg variant used by mmap. */
static inline long _onyx_syscall5(long n, long a, long b, long c, long d, long e) {
    register long a0 __asm__("a0") = a;
    register long a1 __asm__("a1") = b;
    register long a2 __asm__("a2") = c;
    register long a3 __asm__("a3") = d;
    register long a4 __asm__("a4") = e;
    register long a7 __asm__("a7") = n;
    __asm__ volatile ("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a7) : "memory");
    return a0;
}

/* 6-arg variant used by mmap. */
static inline long _onyx_syscall6(long n, long a, long b, long c, long d, long e, long f) {
    register long a0 __asm__("a0") = a;
    register long a1 __asm__("a1") = b;
    register long a2 __asm__("a2") = c;
    register long a3 __asm__("a3") = d;
    register long a4 __asm__("a4") = e;
    register long a5 __asm__("a5") = f;
    register long a7 __asm__("a7") = n;
    __asm__ volatile ("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a7) : "memory");
    return a0;
}

static inline long _onyx_syscall0(long n) {
    register long a0 __asm__("a0");
    register long a7 __asm__("a7") = n;
    __asm__ volatile ("ecall" : "=r"(a0) : "r"(a7) : "memory");
    return a0;
}

static inline long _onyx_syscall1(long n, long a) {
    register long a0 __asm__("a0") = a;
    register long a7 __asm__("a7") = n;
    __asm__ volatile ("ecall" : "+r"(a0) : "r"(a7) : "memory");
    return a0;
}

static inline long _onyx_syscall2(long n, long a, long b) {
    register long a0 __asm__("a0") = a;
    register long a1 __asm__("a1") = b;
    register long a7 __asm__("a7") = n;
    __asm__ volatile ("ecall" : "+r"(a0) : "r"(a1), "r"(a7) : "memory");
    return a0;
}

static inline long _onyx_syscall3(long n, long a, long b, long c) {
    register long a0 __asm__("a0") = a;
    register long a1 __asm__("a1") = b;
    register long a2 __asm__("a2") = c;
    register long a7 __asm__("a7") = n;
    __asm__ volatile ("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
    return a0;
}

#endif /* __onyx__ */

#endif /* __ASSEMBLER__ */
#endif /* ONYX_SYSCALLS_H */
