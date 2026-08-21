/*
 * unistd.h — POSIX standard symbolic constants (libonyxc v0.5).
 *
 * This header aggregates the syscall wrappers (open, close, read, write,
 * lseek, stat, fstat, unlink, mkdir, chdir, getcwd, isatty, fork, execv,
 * execve, getpid, getppid, getuid, getgid, dup, pipe, fcntl, ioctl, sbrk,
 * kill, raise, truncate, ftruncate, symlink, readlink, fsync, access,
 * chmod, fchmod, chown, setuid, setgid, getentropy).
 *
 * All wrappers translate negative returns into -1 + errno.
 */
#ifndef _ONYX_UNISTD_H
#define _ONYX_UNISTD_H

#include <stddef.h>
#include <stdint.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STDIN_FILENO    0
#define STDOUT_FILENO   1
#define STDERR_FILENO   2

#define SEEK_SET    0
#define SEEK_CUR    1
#define SEEK_END    2

#define F_OK    0
#define R_OK    4
#define W_OK    2
#define X_OK    1

/* Forward-declare struct stat from onyxc.h. */
struct stat;

/* Raw syscall declarations (defined in syscalls.c, declared in onyxc.h). */
long _onyx_write(int fd, const void *buf, size_t n);
long _onyx_read(int fd, void *buf, size_t n);
void _onyx_exit(int code);
long _onyx_yield(void);
long _onyx_getpid(void);
long _onyx_getppid(void);
void *_onyx_sbrk(long inc);
long _onyx_brk(long addr);
long _onyx_open(const char *path, int flags, int mode);
long _onyx_close(int fd);
long _onyx_lseek(int fd, long off, int whence);
long _onyx_stat(const char *path, void *st);
long _onyx_fstat(int fd, void *st);
long _onyx_exec(const char *path, char *const *argv);
long _onyx_execve(const char *path, char *const *argv, char *const *envp);
long _onyx_spawn(const char *path, char *const *argv, int ring_hint);
long _onyx_wait(int *status);
long _onyx_waitpid(int pid, int *status, int options);
long _onyx_fork(void);
long _onyx_readdir(const char *dir, char *name_out, size_t len);
long _onyx_getdents64(int fd, void *buf, size_t len);
long _onyx_getring(void);
long _onyx_dropring(int target);
long _onyx_create(const char *path, int mode, long reserved);
long _onyx_mkdir(const char *path);
long _onyx_unlink(const char *path);
long _onyx_rename(const char *oldp, const char *newp);
long _onyx_truncate2(const char *path, long length);
long _onyx_ftruncate(int fd, long length);
long _onyx_chmod(const char *path, int mode);
long _onyx_fchmod(int fd, int mode);
long _onyx_access(const char *path, int mode);
long _onyx_chdir(const char *path);
long _onyx_getcwd(char *buf, size_t len);
long _onyx_dup(int oldfd);
long _onyx_pipe(int *pipefd);
long _onyx_fcntl(int fd, int cmd, long arg);
long _onyx_ioctl(int fd, long request, long arg);
long _onyx_isatty(int fd);
long _onyx_fsync(int fd);
long _onyx_getuid(void);
long _onyx_getgid(void);
long _onyx_setuid(int uid);
long _onyx_setgid(int gid);
long _onyx_readlink(const char *path, char *buf, size_t bufsiz);
long _onyx_symlink(const char *target, const char *linkpath);
long _onyx_chown(const char *path, int uid, int gid);
long _onyx_getentropy(void *buf, size_t len);
long _onyx_kill(int pid, int sig);

/* Inline wrappers with errno handling. */
static inline int open(const char *path, int flags, int mode) {
    long r = _onyx_open(path, flags, mode);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (int)r;
}
static inline int close(int fd) {
    long r = _onyx_close(fd);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (int)r;
}
static inline long read(int fd, void *buf, size_t n) {
    long r = _onyx_read(fd, buf, n);
    if (r < 0) { errno = (int)(-r); return -1; }
    return r;
}
static inline long write(int fd, const void *buf, size_t n) {
    long r = _onyx_write(fd, buf, n);
    if (r < 0) { errno = (int)(-r); return -1; }
    return r;
}
static inline long lseek(int fd, long off, int whence) {
    long r = _onyx_lseek(fd, off, whence);
    if (r < 0) { errno = (int)(-r); return -1; }
    return r;
}
static inline int stat(const char *path, struct stat *st) {
    long r = _onyx_stat(path, st);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (int)r;
}
static inline int fstat(int fd, struct stat *st) {
    long r = _onyx_fstat(fd, st);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (int)r;
}
static inline int unlink(const char *path) {
    long r = _onyx_unlink(path);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (int)r;
}
static inline int rmdir(const char *path) {
    long r = _onyx_unlink(path);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (int)r;
}
static inline int mkdir(const char *path, int mode) {
    (void)mode;
    long r = _onyx_mkdir(path);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (int)r;
}
static inline int chdir(const char *path) {
    long r = _onyx_chdir(path);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (int)r;
}
static inline char *getcwd(char *buf, size_t len) {
    long r = _onyx_getcwd(buf, len);
    if (r < 0) { errno = (int)(-r); return NULL; }
    return buf;
}
static inline int isatty(int fd) { return (int)_onyx_isatty(fd); }
static inline int fork(void) {
    long r = _onyx_fork();
    if (r < 0) { errno = (int)(-r); return -1; }
    return (int)r;
}
static inline int execv(const char *path, char *const *argv) {
    long r = _onyx_exec(path, argv);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (int)r;
}
static inline int execve(const char *path, char *const *argv, char *const *envp) {
    long r = _onyx_execve(path, argv, envp);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (int)r;
}
static inline int getpid(void) { return (int)_onyx_getpid(); }
static inline int getppid(void) { return (int)_onyx_getppid(); }
static inline int getuid(void) { return (int)_onyx_getuid(); }
static inline int getgid(void) { return (int)_onyx_getgid(); }
static inline int dup(int oldfd) {
    long r = _onyx_dup(oldfd);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (int)r;
}
static inline int pipe(int *pipefd) {
    long r = _onyx_pipe(pipefd);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (int)r;
}
static inline int fcntl(int fd, int cmd, long arg) {
    long r = _onyx_fcntl(fd, cmd, arg);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (int)r;
}
static inline int ioctl(int fd, long req, long arg) {
    long r = _onyx_ioctl(fd, req, arg);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (int)r;
}
static inline void *sbrk(long inc) { return _onyx_sbrk(inc); }

/* New v0.5 wrappers. */
static inline int truncate(const char *path, long length) {
    long r = _onyx_truncate2(path, length);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (int)r;
}
static inline int ftruncate(int fd, long length) {
    long r = _onyx_ftruncate(fd, length);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (int)r;
}
static inline int symlink(const char *target, const char *linkpath) {
    long r = _onyx_symlink(target, linkpath);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (int)r;
}
static inline long readlink(const char *path, char *buf, size_t bufsiz) {
    long r = _onyx_readlink(path, buf, bufsiz);
    if (r < 0) { errno = (int)(-r); return -1; }
    return r;
}
static inline int fsync(int fd) {
    long r = _onyx_fsync(fd);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (int)r;
}
static inline int access(const char *path, int mode) {
    long r = _onyx_access(path, mode);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (int)r;
}
static inline int chmod(const char *path, int mode) {
    long r = _onyx_chmod(path, mode);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (int)r;
}
static inline int fchmod(int fd, int mode) {
    long r = _onyx_fchmod(fd, mode);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (int)r;
}
static inline int chown(const char *path, int uid, int gid) {
    long r = _onyx_chown(path, uid, gid);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (int)r;
}
static inline int setuid(int uid) {
    long r = _onyx_setuid(uid);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (int)r;
}
static inline int setgid(int gid) {
    long r = _onyx_setgid(gid);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (int)r;
}
static inline int seteuid(int uid) { return setuid(uid); }
static inline int setegid(int gid) { return setgid(gid); }
static inline int getentropy(void *buf, size_t len) {
    long r = _onyx_getentropy(buf, len);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (int)r;
}

/* sleep/usleep — provided by time.c via nanosleep */
int usleep(unsigned long us);
unsigned int sleep(unsigned int sec);

/* execvpe — exec with both argv and envp, plus PATH lookup */
int execvpe(const char *file, char *const *argv, char *const *envp);

/* getwd — deprecated but standard */
char *getwd(char *buf);

#ifdef __cplusplus
}
#endif

#endif /* _ONYX_UNISTD_H */
