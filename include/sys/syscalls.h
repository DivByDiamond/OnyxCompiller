/*
 * syscalls.h — OnyxOS syscall ABI.
 * Synced with OnyxKernel/kernel/src/syscall/abi.rs (v0.4).
 *
 * Calling convention (RISC-V LP64):
 *   a7 = syscall number
 *   a0..a5 = arguments
 *   a0 = return value (negative = -errno)
 *
 * Trap: ecall instruction (CAUSE_U_ECALL handled by kernel/src/srv/trap.rs).
 *
 * Ring ACL (kernel/src/syscall/handler.rs):
 *   Ring 2 (user) may call: write, read, exit, yield, getpid, sbrk,
 *     open, close, lseek, stat, fstat, exec, readdir, getring, dropring,
 *     sigmask, write_fd, brk, mmap, munmap, mprotect, dup, chdir, getcwd,
 *     access, gettimeofday, fcntl, getuid, getgid, uname, nanosleep,
 *     clock_gettime, clock_getres, isatty, getentropy, getdents64, ioctl,
 *     sigaction, sigprocmask, sigreturn, execve, getppid, fork, waitpid,
 *     chan_connect, chan_send, chan_recv, chan_close, chan_open.
 *   Ring 1 (root) additionally: spawn, wait, kill, create, mkdir, unlink,
 *     rename, truncate, truncate2, ftruncate, utimens, pipe, setuid, setgid,
 *     fsync, symlink, chmod, fchmod, chan_create, chan_create_named,
 *     snapshot_*.
 */
#ifndef ONYX_SYSCALLS_H
#define ONYX_SYSCALLS_H

#include <stdint.h>

/* ── Original syscalls 1–49 (v0.3) ────────────────────────────────────── */
#define SYS_write             1
#define SYS_read              2
#define SYS_exit              3
#define SYS_yield             4
#define SYS_getpid            5
#define SYS_brk               6
#define SYS_mmap              7
#define SYS_open              8
#define SYS_close             9
#define SYS_lseek            10
#define SYS_stat             11
#define SYS_exec             12
#define SYS_sbrk             13
#define SYS_spawn            14
#define SYS_wait             15
#define SYS_readdir          16
#define SYS_getring          17
#define SYS_dropring         18
#define SYS_snapshot_create  19
#define SYS_snapshot_rollback 20
#define SYS_snapshot_list    21
#define SYS_kill             22
#define SYS_sigmask          23
#define SYS_write_fd         24
#define SYS_create           25
#define SYS_mkdir            26
#define SYS_chan_create      27
#define SYS_chan_connect     28
#define SYS_chan_send        29
#define SYS_chan_recv        30
#define SYS_chan_close       31
#define SYS_chan_create_named 32
#define SYS_chan_open        33
#define SYS_munmap           34
#define SYS_dup              35
#define SYS_pipe             36
#define SYS_unlink           37
#define SYS_rename           38
#define SYS_chdir            39
#define SYS_getcwd           40
#define SYS_truncate         41   /* legacy: truncates to 0 */
#define SYS_access           42
#define SYS_gettimeofday     43
#define SYS_fcntl            44
#define SYS_getuid           45
#define SYS_getgid           46
#define SYS_utimens          47
#define SYS_uname            48
#define SYS_nanosleep        49

/* ── New syscalls 50–77 (v0.4 — userspace-readiness update) ───────────── */
#define SYS_fstat            50   /* fstat(fd, struct stat *) */
#define SYS_waitpid          51   /* waitpid(pid, *status, options) */
#define SYS_getdents64       52   /* getdents64(fd, *buf, len) */
#define SYS_ioctl            53   /* ioctl(fd, req, arg) */
#define SYS_mprotect         54   /* mprotect(addr, len, prot) */
#define SYS_sigaction        55   /* sigaction(signum, *act, *oldact) */
#define SYS_sigprocmask      56   /* sigprocmask(how, *set, *oldset) */
#define SYS_sigreturn        57   /* sigreturn() */
#define SYS_execve           58   /* execve(path, argv, envp) */
#define SYS_getppid          59   /* getppid() */
#define SYS_setpgid          60   /* setpgid(pid, pgid) */
#define SYS_setsid           61   /* setsid() */
#define SYS_getpgid          62   /* getpgid(pid) */
#define SYS_fork             63   /* fork() — vfork-style */
#define SYS_clock_gettime    64   /* clock_gettime(clk_id, *ts) */
#define SYS_clock_getres     65   /* clock_getres(clk_id, *res) */
#define SYS_isatty           66   /* isatty(fd) */
#define SYS_getentropy       67   /* getentropy(buf, len) */
#define SYS_setuid           68   /* setuid(uid) — root only */
#define SYS_setgid           69   /* setgid(gid) — root only */
#define SYS_fsync            70   /* fsync(fd) */
#define SYS_truncate2        71   /* truncate(path, length) — POSIX */
#define SYS_ftruncate        72   /* ftruncate(fd, length) */
#define SYS_readlink         73   /* readlink(path, buf, len) */
#define SYS_symlink          74   /* symlink(target, linkpath) */
#define SYS_chmod            75   /* chmod(path, mode) */
#define SYS_fchmod           76   /* fchmod(fd, mode) */
#define SYS_getdents         77   /* compat alias for getdents64 */

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

#endif /* __ASSEMBLER__ */
#endif /* ONYX_SYSCALLS_H */
