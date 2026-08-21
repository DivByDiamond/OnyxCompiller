/*
 * linux_stubs.c — Linux/x86_64 stubs for OnyxOS syscalls,
 * used only for host-side unit-testing of libonyxc.
 *
 * DO NOT link this file when building for OnyxOS — OnyxOS provides
 * real implementations in src/core/syscalls.c (RISC-V ecall-based).
 *
 * We deliberately avoid including <onyxc.h> here because that would
 * pull in our static inline wrappers (open/close/read/write/etc.),
 * which would shadow the glibc declarations.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/syscall.h>

/* glibc defines macros that rewrite st_atime → st_atim.tv_sec etc.,
 * which break our onyx_stat struct. Undef them. */
#ifdef st_atime
#undef st_atime
#endif
#ifdef st_mtime
#undef st_mtime
#endif
#ifdef st_ctime
#undef st_ctime
#endif

/* glibc hides setpgid/getpgid/setsid behind _GNU_SOURCE or in <unistd.h>
 * with proper feature macros. Use direct syscall on Linux as fallback. */
static long lin_setpgid(int pid, int pgid) { return syscall(SYS_setpgid, pid, pgid); }
static long lin_getpgid(int pid) { return syscall(SYS_getpgid, pid); }
static long lin_setsid(void) { return syscall(SYS_setsid); }

/* OnyxOS-compatible struct stat (matches libonyxc/include/core/onyxc.h). */
struct onyx_stat {
    unsigned long long st_dev;
    unsigned long long st_ino;
    unsigned int st_mode;
    unsigned int st_nlink;
    unsigned int st_uid;
    unsigned int st_gid;
    unsigned int __pad0;
    unsigned long long st_rdev;
    long long st_size;
    long long st_blksize;
    long long st_blocks;
    long long st_atime;
    long long st_atime_nsec;
    long long st_mtime;
    long long st_mtime_nsec;
    long long st_ctime;
    long long st_ctime_nsec;
    long long __unused[3];
};

extern char **environ;

long _onyx_write(int fd, const void *buf, size_t n) { return (long)write(fd, buf, n); }
long _onyx_read(int fd, void *buf, size_t n) { return (long)read(fd, buf, n); }
void _onyx_exit(int code) { exit(code); }
long _onyx_yield(void) { return 0; }
long _onyx_getpid(void) { return (long)getpid(); }
long _onyx_getppid(void) { return (long)getppid(); }

static unsigned char *g_pool = NULL;
static size_t g_pool_used = 0;
static size_t g_pool_size = 0;
void *_onyx_sbrk(long inc) {
    if (!g_pool) {
        g_pool_size = 16 * 1024 * 1024;
        g_pool = (unsigned char *)malloc(g_pool_size);
    }
    if (inc == 0) return (void *)(g_pool + g_pool_used);
    if (g_pool_used + (size_t)inc > g_pool_size) return (void *)-1;
    void *p = (void *)(g_pool + g_pool_used);
    g_pool_used += (size_t)inc;
    return p;
}
long _onyx_brk(long addr) { (void)addr; return 0; }

long _onyx_open(const char *path, int flags, int mode) {
    return (long)open(path, flags, mode);
}
long _onyx_close(int fd) { return (long)close(fd); }
long _onyx_lseek(int fd, long off, int whence) { return (long)lseek(fd, off, whence); }

long _onyx_stat(const char *path, void *st) {
    struct stat linux_st;
    if (stat(path, &linux_st) < 0) return -errno;
    struct onyx_stat *o = (struct onyx_stat *)st;
    o->st_dev = linux_st.st_dev;
    o->st_ino = linux_st.st_ino;
    o->st_mode = linux_st.st_mode;
    o->st_nlink = linux_st.st_nlink;
    o->st_uid = linux_st.st_uid;
    o->st_gid = linux_st.st_gid;
    o->st_rdev = linux_st.st_rdev;
    o->st_size = linux_st.st_size;
    o->st_blksize = linux_st.st_blksize;
    o->st_blocks = linux_st.st_blocks;
    o->st_atime = linux_st.st_atim.tv_sec;
    o->st_atime_nsec = linux_st.st_atim.tv_nsec;
    o->st_mtime = linux_st.st_mtim.tv_sec;
    o->st_mtime_nsec = linux_st.st_mtim.tv_nsec;
    o->st_ctime = linux_st.st_ctim.tv_sec;
    o->st_ctime_nsec = linux_st.st_ctim.tv_nsec;
    return 0;
}
long _onyx_fstat(int fd, void *st) {
    struct stat linux_st;
    if (fstat(fd, &linux_st) < 0) return -errno;
    struct onyx_stat *o = (struct onyx_stat *)st;
    o->st_dev = linux_st.st_dev;
    o->st_ino = linux_st.st_ino;
    o->st_mode = linux_st.st_mode;
    o->st_nlink = linux_st.st_nlink;
    o->st_uid = linux_st.st_uid;
    o->st_gid = linux_st.st_gid;
    o->st_rdev = linux_st.st_rdev;
    o->st_size = linux_st.st_size;
    o->st_blksize = linux_st.st_blksize;
    o->st_blocks = linux_st.st_blocks;
    o->st_atime = linux_st.st_atim.tv_sec;
    o->st_atime_nsec = linux_st.st_atim.tv_nsec;
    o->st_mtime = linux_st.st_mtim.tv_sec;
    o->st_mtime_nsec = linux_st.st_mtim.tv_nsec;
    o->st_ctime = linux_st.st_ctim.tv_sec;
    o->st_ctime_nsec = linux_st.st_ctim.tv_nsec;
    return 0;
}

long _onyx_exec(const char *path, char *const *argv) { return (long)execv(path, argv); }
long _onyx_execve(const char *path, char *const *argv, char *const *envp) { return (long)execve(path, argv, envp); }
long _onyx_spawn(const char *path, char *const *argv, int ring_hint) {
    (void)ring_hint;
    pid_t p = fork();
    if (p == 0) { execv(path, argv); exit(127); }
    if (p < 0) return -1;
    int st;
    waitpid(p, &st, 0);
    return (long)p;
}
long _onyx_wait(int *status) { return (long)wait(status); }
long _onyx_waitpid(int pid, int *status, int options) { return (long)waitpid(pid, status, options); }
long _onyx_fork(void) { return (long)fork(); }
long _onyx_readdir(const char *dir, char *name_out, size_t len) { (void)dir; (void)name_out; (void)len; return -1; }
long _onyx_getdents64(int fd, void *buf, size_t len) { (void)fd; (void)buf; (void)len; return -1; }
long _onyx_getring(void) { return 2; }
long _onyx_dropring(int target) { (void)target; return 0; }
long _onyx_create(const char *path, int mode, long reserved) {
    (void)reserved;
    return (long)open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
}
long _onyx_mkdir(const char *path) { return (long)mkdir(path, 0755); }
long _onyx_unlink(const char *path) { return (long)unlink(path); }
long _onyx_rename(const char *oldp, const char *newp) { return (long)rename(oldp, newp); }
long _onyx_truncate2(const char *path, long length) { return (long)truncate(path, (off_t)length); }
long _onyx_ftruncate(int fd, long length) { return (long)ftruncate(fd, (off_t)length); }
long _onyx_chmod(const char *path, int mode) { return (long)chmod(path, (mode_t)mode); }
long _onyx_fchmod(int fd, int mode) { return (long)fchmod(fd, (mode_t)mode); }
long _onyx_access(const char *path, int mode) { return (long)access(path, mode); }
long _onyx_chdir(const char *path) { return (long)chdir(path); }
long _onyx_getcwd(char *buf, size_t len) { return getcwd(buf, len) ? 0 : -errno; }
long _onyx_dup(int oldfd) { return (long)dup(oldfd); }
long _onyx_pipe(int *pipefd) { return (long)pipe(pipefd); }
long _onyx_fcntl(int fd, int cmd, long arg) { return (long)fcntl(fd, cmd, arg); }
long _onyx_ioctl(int fd, long req, long arg) { (void)fd; (void)req; (void)arg; return -1; }
long _onyx_isatty(int fd) { return (long)isatty(fd); }
long _onyx_fsync(int fd) { return (long)fsync(fd); }
long _onyx_getuid(void) { return (long)getuid(); }
long _onyx_getgid(void) { return (long)getgid(); }
long _onyx_setuid(int uid) { return (long)setuid((uid_t)uid); }
long _onyx_setgid(int gid) { return (long)setgid((gid_t)gid); }
long _onyx_readlink(const char *path, char *buf, size_t bufsiz) {
    return (long)readlink(path, buf, bufsiz);
}
long _onyx_symlink(const char *target, const char *linkpath) {
    return (long)symlink(target, linkpath);
}
long _onyx_chown(const char *path, int uid, int gid) { return (long)chown(path, (uid_t)uid, (gid_t)gid); }
long _onyx_getentropy(void *buf, size_t len) {
    int fd = open("/dev/urandom", O_RDONLY, 0);
    if (fd < 0) return -errno;
    ssize_t n = read(fd, buf, len);
    close(fd);
    return (n == (ssize_t)len) ? 0 : -EIO;
}
long _onyx_kill(int pid, int sig) { return (long)kill((pid_t)pid, sig); }
long _onyx_sigaction(int sig, const void *act, void *oldact) {
    /* OnyxOS struct sigaction is compatible with Linux. */
    return (long)sigaction(sig, (const struct sigaction *)act, (struct sigaction *)oldact);
}
long _onyx_sigprocmask(int how, const void *set, void *oldset) {
    return (long)sigprocmask(how, (const sigset_t *)set, (sigset_t *)oldset);
}
long _onyx_sigreturn(void) { return 0; }
long _onyx_uname(void *buf) { (void)buf; return 0; }
long _onyx_clock_gettime(long clk_id, void *ts) {
    return (long)clock_gettime((clockid_t)clk_id, (struct timespec *)(void *)ts);
}
long _onyx_clock_getres(long clk_id, void *res) {
    return (long)clock_getres((clockid_t)clk_id, (struct timespec *)(void *)res);
}
long _onyx_nanosleep(const void *req, void *rem) {
    return (long)nanosleep((const struct timespec *)(const void *)req, (struct timespec *)(void *)rem);
}
long _onyx_gettimeofday(void *tv) {
    struct timeval linux_tv;
    long r = (long)syscall(SYS_gettimeofday, &linux_tv, NULL);
    if (r == 0) {
        memcpy(tv, &linux_tv, sizeof(linux_tv));
    }
    return r;
}
long _onyx_setpgid(int pid, int pgid) { return lin_setpgid(pid, pgid); }
long _onyx_getpgid(int pid) { return lin_getpgid(pid); }
long _onyx_setsid(void) { return lin_setsid(); }
long _onyx_utimens(const char *path, const void *times) { (void)path; (void)times; return 0; }
long _onyx_chan_create(void) { return -1; }
long _onyx_chan_create_named(const char *n) { (void)n; return -1; }
long _onyx_chan_open(const char *name) { (void)name; return -1; }
long _onyx_chan_connect(int chan_id) { (void)chan_id; return -1; }
long _onyx_chan_send(int chan_id, const void *buf, size_t len) { (void)chan_id; (void)buf; (void)len; return -1; }
long _onyx_chan_recv(int chan_id, void *buf, size_t len) { (void)chan_id; (void)buf; (void)len; return -1; }
long _onyx_chan_close(int chan_id) { (void)chan_id; return -1; }
long _onyx_snapshot_create(const char *name) { (void)name; return -1; }
long _onyx_snapshot_rollback(int id) { (void)id; return -1; }
long _onyx_snapshot_list(void *buf, size_t len) { (void)buf; (void)len; return -1; }
long _onyx_write_fd(int fd, const void *buf, size_t n) { return (long)write(fd, buf, n); }
