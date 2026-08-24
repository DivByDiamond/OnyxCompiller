/*
 * libonyxc — C library for OnyxOS userspace (v0.5).
 *
 * This is the umbrella header. Including <onyxc.h> pulls in:
 *   - errno.h, fcntl.h, time.h, unistd.h, signal.h, limits.h
 *   - FILE* opaque type and stdio prototypes
 *   - struct stat / struct tm / struct sigaction definitions
 *   - Raw _onyx_* syscall declarations
 *   - String, ctype, stdlib function prototypes
 *
 * v0.5: FILE* buffered I/O, sprintf/snprintf/sscanf, errno/strerror/perror,
 *   time functions, signal helpers, more stdlib (strtoul, strtoll, strtoull,
 *   strtod, qsort, bsearch, labs, llabs, atexit, itoa), more string
 *   (strnlen, strncat, strrchr, strstr, strcasecmp, strpbrk, strcspn, strspn,
 *   strtok_r, memchr), and unistd wrappers (truncate, symlink, readlink,
 *   fsync, access, chmod, fchmod, chown, setuid, setgid, getentropy).
 */
#ifndef LIBONYXC_H
#define LIBONYXC_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>

/* ── Program arguments — set by _start (start_cc.c). Use these when
 *    main's (argc, argv) parameters are unavailable or unreliable. */
extern char **__onyx_argv;

/* ── Standard file descriptors (also defined in unistd.h, but kept here
 *    for backwards compatibility with code that doesn't include unistd.h). */
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* ── Linux-compatible struct stat (matches kernel UserStat). */
struct stat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t __pad0;
    uint64_t st_rdev;
    int64_t  st_size;
    int64_t  st_blksize;
    int64_t  st_blocks;
    int64_t  st_atime;
    int64_t  st_atime_nsec;
    int64_t  st_mtime;
    int64_t  st_mtime_nsec;
    int64_t  st_ctime;
    int64_t  st_ctime_nsec;
    int64_t  __unused[3];
};

/* ── FILE stream — opaque to user code; full definition in stdio.c. */
struct __FILE_s;
typedef struct __FILE_s FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

/* ── errno — defined as `(*___errno_location())` so library code can
 *    write to it without user code needing a global. */
extern int *___errno_location(void);
#define errno (*(___errno_location()))

/* ── Standard syscall declarations (defined in syscalls.c). */
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
long _onyx_sigaction(int sig, const void *act, void *oldact);
long _onyx_sigprocmask(int how, const void *set, void *oldset);
long _onyx_sigreturn(void);
long _onyx_uname(void *buf);
long _onyx_clock_gettime(long clk_id, void *ts);
long _onyx_clock_getres(long clk_id, void *res);
long _onyx_nanosleep(const void *req, void *rem);
long _onyx_gettimeofday(void *tv);
long _onyx_setpgid(int pid, int pgid);
long _onyx_getpgid(int pid);
long _onyx_setsid(void);
long _onyx_utimens(const char *path, const void *times);
long _onyx_chan_create(void);
long _onyx_chan_create_named(const char *n);
long _onyx_chan_open(const char *name);
long _onyx_chan_connect(int chan_id);
long _onyx_chan_send(int chan_id, const void *buf, size_t len);
long _onyx_chan_recv(int chan_id, void *buf, size_t len);
long _onyx_chan_close(int chan_id);
long _onyx_snapshot_create(const char *name);
long _onyx_snapshot_rollback(int id);
long _onyx_snapshot_list(void *buf, size_t len);
long _onyx_write_fd(int fd, const void *buf, size_t n);

/* ── stdio (FILE*-based buffered I/O, v0.5). */
int   printf(const char *fmt, ...);
int   fprintf(FILE *fp, const char *fmt, ...);
int   vfprintf(FILE *fp, const char *fmt, va_list ap);
int   vprintf(const char *fmt, va_list ap);
int   sprintf(char *buf, const char *fmt, ...);
int   snprintf(char *buf, size_t cap, const char *fmt, ...);
int   vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap);
int   vsprintf(char *buf, const char *fmt, va_list ap);
int   scanf(const char *fmt, ...);
int   fscanf(FILE *fp, const char *fmt, ...);
int   sscanf(const char *buf, const char *fmt, ...);
int   vsscanf(const char *buf, const char *fmt, va_list ap);
int   vfscanf(FILE *fp, const char *fmt, va_list ap);

int   puts(const char *s);
int   putchar(int c);
int   fflush(FILE *fp);

FILE *fopen(const char *path, const char *mode);
int   fclose(FILE *fp);
size_t fread(void *buf, size_t sz, size_t count, FILE *fp);
size_t fwrite(const void *buf, size_t sz, size_t count, FILE *fp);
char *fgets(char *buf, int size, FILE *fp);
int   fputs(const char *s, FILE *fp);
int   fgetc(FILE *fp);
int   fputc(int ch, FILE *fp);
int   getc(FILE *fp);
int   putc(int ch, FILE *fp);
int   getchar(void);
long  getline(char **lineptr, size_t *n, FILE *fp);
long  getdelim(char **lineptr, size_t *n, int delim, FILE *fp);
int   fseek(FILE *fp, long offset, int whence);
long  ftell(FILE *fp);
int   feof(FILE *fp);
int   ferror(FILE *fp);
void  clearerr(FILE *fp);
void  rewind(FILE *fp);
int   fileno(FILE *fp);
int   remove(const char *path);
int   rename(const char *oldp, const char *newp);
FILE *tmpfile(void);
char *tmpnam(char *buf);

size_t strlen(const char *s);

/* ── stdlib. */
void *malloc(size_t n);
void  free(void *p);
void *calloc(size_t n, size_t sz);
void *realloc(void *p, size_t n);
void  exit(int code);
void  abort(void);
int   abs(int n);
int   atoi(const char *s);
long  strtol(const char *s, char **endp, int base);
unsigned long strtoul(const char *s, char **endp, int base);
long long strtoll(const char *s, char **endp, int base);
unsigned long long strtoull(const char *s, char **endp, int base);
double strtod(const char *s, char **endp);
char *getenv(const char *name);
int   setenv(const char *name, const char *value, int overwrite);
int   unsetenv(const char *name);
long   labs(long n);
long long llabs(long long n);
char  *itoa(int v, char *buf, int base);
int    atexit(void (*fn)(void));
int    atexit_run_handlers(void);
int    atexit_count_get(void);
char **environ_get(void);

/* ── string. */
char *strcpy(char *d, const char *s);
char *strncpy(char *d, const char *s, size_t n);
int   strcmp(const char *a, const char *b);
int   strncmp(const char *a, const char *b, size_t n);
char *strcat(char *d, const char *s);
char *strncat(char *d, const char *s, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strdup(const char *s);
size_t strnlen(const char *s, size_t n);
char  *strstr(const char *haystack, const char *needle);
int    strcasecmp(const char *a, const char *b);
int    strncasecmp(const char *a, const char *b, size_t n);
char  *strpbrk(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);
size_t strspn(const char *s, const char *accept);
char  *strtok_r(char *str, const char *delim, char **saveptr);
void  *memcpy(void *d, const void *s, size_t n);
void  *memset(void *d, int c, size_t n);
void  *memmove(void *d, const void *s, size_t n);
int   memcmp(const void *a, const void *b, size_t n);
void  *memchr(const void *s, int c, size_t n);

/* ── ctype. */
int isalpha(int c);
int isdigit(int c);
int isspace(int c);
int isupper(int c);
int islower(int c);
int isxdigit(int c);
int isalnum(int c);
int ispunct(int c);
int isprint(int c);
int iscntrl(int c);
int isgraph(int c);
int tolower(int c);
int toupper(int c);

/* ── strerror / perror (v0.5). */
char *strerror(int err);
void  perror(const char *s);

/* ── Sort / search (v0.5). */
void  qsort(void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *));
void  *bsearch(const void *key, const void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *));

/* ── Standard headers — pull in everything else. */
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <limits.h>
#include <termios.h>
#include <math.h>

#endif /* LIBONYXC_H */
