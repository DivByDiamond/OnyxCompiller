/*
 * syscalls.c — raw syscall wrappers (v0.4).
 *
 * Covers the original v0.3 set (1–31) plus the v0.4 additions (32–77).
 * Each wrapper is a thin shim over the `_onyx_syscallN` inline helpers in
 * include/syscalls.h. The libc-level wrappers (printf, fopen, getenv, ...)
 * live in stdio.c / stdlib.c / unistd.c and call into these.
 */
#include "onyxc.h"
#include "../../../include/sys/syscalls.h"

/* ── Original v0.3 set (1–31) ─────────────────────────────────────────── */

long _onyx_write(int fd, const void *buf, size_t n) {
    return _onyx_syscall3(SYS_write, fd, (long)buf, (long)n);
}

long _onyx_read(int fd, void *buf, size_t n) {
    return _onyx_syscall3(SYS_read, fd, (long)buf, (long)n);
}

void _onyx_exit(int code) {
    _onyx_syscall1(SYS_exit, code);
    /* Should not return, but just in case. */
    for (;;) {
        _onyx_syscall0(SYS_yield);
    }
}

long _onyx_yield(void)            { return _onyx_syscall0(SYS_yield); }
long _onyx_getpid(void)           { return _onyx_syscall0(SYS_getpid); }

void *_onyx_sbrk(long inc) {
    long r = _onyx_syscall1(SYS_sbrk, inc);
    /* Kernel returns negative errno on failure (e.g. -ENOMEM = -12).
     * Convert to canonical (void *)-1 so libc callers can detect it. */
    if (r < 0) return (void *)-1;
    return (void *)r;
}

long _onyx_brk(long addr) {
    return _onyx_syscall1(SYS_brk, addr);
}

long _onyx_open(const char *path, int flags, int mode) {
    return _onyx_syscall3(SYS_open, (long)path, flags, mode);
}

long _onyx_close(int fd) {
    return _onyx_syscall1(SYS_close, fd);
}

long _onyx_lseek(int fd, long off, int whence) {
    return _onyx_syscall3(SYS_lseek, fd, off, whence);
}

long _onyx_stat(const char *path, void *st) {
    return _onyx_syscall2(SYS_stat, (long)path, (long)st);
}

long _onyx_fstat(int fd, void *st) {
    return _onyx_syscall2(SYS_fstat, fd, (long)st);
}

long _onyx_exec(const char *path, char *const *argv) {
    return _onyx_syscall2(SYS_exec, (long)path, (long)argv);
}

long _onyx_execve(const char *path, char *const *argv, char *const *envp) {
    return _onyx_syscall3(SYS_execve, (long)path, (long)argv, (long)envp);
}

long _onyx_spawn(const char *path, char *const *argv, int ring_hint) {
    return _onyx_syscall3(SYS_spawn, (long)path, (long)argv, ring_hint);
}

long _onyx_wait(int *status) {
    return _onyx_syscall1(SYS_wait, (long)status);
}

long _onyx_waitpid(int pid, int *status, int options) {
    return _onyx_syscall3(SYS_waitpid, pid, (long)status, options);
}

long _onyx_fork(void) {
    return _onyx_syscall0(SYS_fork);
}

long _onyx_getppid(void) {
    return _onyx_syscall0(SYS_getppid);
}

long _onyx_readdir(const char *dir, char *name_out, size_t len) {
    return _onyx_syscall3(SYS_readdir, (long)dir, (long)name_out, (long)len);
}

long _onyx_getdents64(int fd, void *buf, size_t len) {
    return _onyx_syscall3(SYS_getdents64, fd, (long)buf, (long)len);
}

long _onyx_getring(void)             { return _onyx_syscall0(SYS_getring); }
long _onyx_dropring(int target)      { return _onyx_syscall1(SYS_dropring, target); }

/* File ops */
long _onyx_create(const char *path, int mode, long reserved) {
    return _onyx_syscall3(SYS_create, (long)path, mode, reserved);
}
long _onyx_mkdir(const char *path) {
    return _onyx_syscall1(SYS_mkdir, (long)path);
}
long _onyx_unlink(const char *path) {
    return _onyx_syscall1(SYS_unlink, (long)path);
}
long _onyx_rename(const char *oldp, const char *newp) {
    return _onyx_syscall2(SYS_rename, (long)oldp, (long)newp);
}
long _onyx_truncate(const char *path) {
    return _onyx_syscall1(SYS_truncate, (long)path);
}
long _onyx_truncate2(const char *path, long length) {
    return _onyx_syscall2(SYS_truncate2, (long)path, length);
}
long _onyx_ftruncate(int fd, long length) {
    return _onyx_syscall2(SYS_ftruncate, fd, length);
}
long _onyx_chmod(const char *path, int mode) {
    return _onyx_syscall2(SYS_chmod, (long)path, mode);
}
long _onyx_fchmod(int fd, int mode) {
    return _onyx_syscall2(SYS_fchmod, fd, mode);
}
long _onyx_access(const char *path, int mode) {
    return _onyx_syscall2(SYS_access, (long)path, mode);
}
long _onyx_chdir(const char *path) {
    return _onyx_syscall1(SYS_chdir, (long)path);
}
long _onyx_getcwd(char *buf, size_t len) {
    return _onyx_syscall2(SYS_getcwd, (long)buf, (long)len);
}
long _onyx_dup(int oldfd) {
    return _onyx_syscall1(SYS_dup, oldfd);
}
long _onyx_pipe(int *pipefd) {
    return _onyx_syscall1(SYS_pipe, (long)pipefd);
}
long _onyx_fcntl(int fd, int cmd, long arg) {
    return _onyx_syscall3(SYS_fcntl, fd, cmd, arg);
}
long _onyx_ioctl(int fd, long request, long arg) {
    return _onyx_syscall3(SYS_ioctl, fd, request, arg);
}
long _onyx_isatty(int fd) {
    return _onyx_syscall1(SYS_isatty, fd);
}
long _onyx_fsync(int fd) {
    return _onyx_syscall1(SYS_fsync, fd);
}

/* Memory */
long _onyx_mmap(long addr, long len, long prot, long flags, long fd, long off) {
    return _onyx_syscall6(SYS_mmap, addr, len, prot, flags, fd, off);
}
long _onyx_munmap(long addr, long len) {
    return _onyx_syscall2(SYS_munmap, addr, len);
}
long _onyx_mprotect(long addr, long len, long prot) {
    return _onyx_syscall3(SYS_mprotect, addr, len, prot);
}

/* Identity */
long _onyx_getuid(void)  { return _onyx_syscall0(SYS_getuid); }
long _onyx_getgid(void)  { return _onyx_syscall0(SYS_getgid); }
long _onyx_setuid(int uid) { return _onyx_syscall1(SYS_setuid, uid); }
long _onyx_setgid(int gid) { return _onyx_syscall1(SYS_setgid, gid); }
long _onyx_setpgid(int pid, int pgid) { return _onyx_syscall2(SYS_setpgid, pid, pgid); }
long _onyx_getpgid(int pid) { return _onyx_syscall1(SYS_getpgid, pid); }
long _onyx_setsid(void) { return _onyx_syscall0(SYS_setsid); }

/* Time */
long _onyx_gettimeofday(void *tv) {
    return _onyx_syscall1(SYS_gettimeofday, (long)tv);
}
long _onyx_clock_gettime(long clk_id, void *ts) {
    return _onyx_syscall2(SYS_clock_gettime, clk_id, (long)ts);
}
long _onyx_clock_getres(long clk_id, void *res) {
    return _onyx_syscall2(SYS_clock_getres, clk_id, (long)res);
}
long _onyx_nanosleep(const void *req, void *rem) {
    return _onyx_syscall2(SYS_nanosleep, (long)req, (long)rem);
}
long _onyx_utimens(const char *path, const void *times) {
    return _onyx_syscall2(SYS_utimens, (long)path, (long)times);
}

/* Signals */
long _onyx_kill(int pid, int sig) {
    return _onyx_syscall2(SYS_kill, pid, sig);
}
long _onyx_sigmask(int how, int sig) {
    return _onyx_syscall2(SYS_sigmask, how, sig);
}
long _onyx_sigaction(int sig, const void *act, void *oldact) {
    return _onyx_syscall3(SYS_sigaction, sig, (long)act, (long)oldact);
}
long _onyx_sigprocmask(int how, const void *set, void *oldset) {
    return _onyx_syscall3(SYS_sigprocmask, how, (long)set, (long)oldset);
}
long _onyx_sigreturn(void) {
    return _onyx_syscall0(SYS_sigreturn);
}

/* System info */
long _onyx_uname(void *buf) {
    return _onyx_syscall1(SYS_uname, (long)buf);
}

/* Misc */
long _onyx_getentropy(void *buf, size_t len) {
    return _onyx_syscall2(SYS_getentropy, (long)buf, (long)len);
}
long _onyx_readlink(const char *path, char *buf, size_t bufsiz) {
    return _onyx_syscall3(SYS_readlink, (long)path, (long)buf, (long)bufsiz);
}
long _onyx_symlink(const char *target, const char *linkpath) {
    return _onyx_syscall2(SYS_symlink, (long)target, (long)linkpath);
}

/* IPC channels */
long _onyx_chan_create(void)              { return _onyx_syscall0(SYS_chan_create); }
long _onyx_chan_create_named(const char *n) { return _onyx_syscall1(SYS_chan_create_named, (long)n); }
long _onyx_chan_open(const char *name)    { return _onyx_syscall1(SYS_chan_open, (long)name); }
long _onyx_chan_connect(int chan_id)      { return _onyx_syscall1(SYS_chan_connect, chan_id); }
long _onyx_chan_send(int chan_id, const void *buf, size_t len) {
    return _onyx_syscall3(SYS_chan_send, chan_id, (long)buf, (long)len);
}
long _onyx_chan_recv(int chan_id, void *buf, size_t len) {
    return _onyx_syscall3(SYS_chan_recv, chan_id, (long)buf, (long)len);
}
long _onyx_chan_close(int chan_id)        { return _onyx_syscall1(SYS_chan_close, chan_id); }

/* Snapshots (root only) */
long _onyx_snapshot_create(const char *name) {
    return _onyx_syscall1(SYS_snapshot_create, (long)name);
}
long _onyx_snapshot_rollback(int id) {
    return _onyx_syscall1(SYS_snapshot_rollback, id);
}
long _onyx_snapshot_list(void *buf, size_t len) {
    return _onyx_syscall2(SYS_snapshot_list, (long)buf, (long)len);
}

/* File-descriptor write (bypasses the stdio fd 1/2 → UART shortcut). */
long _onyx_write_fd(int fd, const void *buf, size_t n) {
    return _onyx_syscall3(SYS_write_fd, fd, (long)buf, (long)n);
}

/* poll(2) — non-blocking fd readiness multiplexer (OnyxKernel SYS_poll).
 * fds points to an array of `struct pollfd` (16 bytes per entry on Onyx:
 * 64-bit fd token + 32-bit events + 32-bit revents, see include/io/poll.h). */
long _onyx_poll(void *fds, unsigned long nfds, int timeout) {
    return _onyx_syscall3(SYS_poll, (long)fds, (long)nfds, (long)timeout);
}

/* TCP (OnyxKernel/kernel/src/syscall/net_sys.rs). ip is 4 raw bytes,
 * network byte order; conn_id is an opaque handle (0..7). */
long _onyx_net_connect(const unsigned char ip[4], int port) {
    return _onyx_syscall2(SYS_net_connect, (long)ip, port);
}
long _onyx_net_send(long conn_id, const void *buf, size_t n) {
    return _onyx_syscall3(SYS_net_send, conn_id, (long)buf, (long)n);
}
long _onyx_net_recv(long conn_id, void *buf, size_t n) {
    return _onyx_syscall3(SYS_net_recv, conn_id, (long)buf, (long)n);
}
long _onyx_net_close(long conn_id) {
    return _onyx_syscall1(SYS_net_close, conn_id);
}
