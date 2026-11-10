/*
 * shim.c — freestanding libc shim for OnyxCC running on OnyxOS.
 *
 * Implements the subset of libc functions that OnyxCC uses:
 *   - stdio: fopen/fclose/fread/fwrite/fseek/ftell/fprintf/printf/putchar/fputc
 *   - stdlib: malloc/free/calloc/realloc/exit/getenv/strtod
 *   - string: strlen/strcmp/strncmp/strchr/strrchr/strstr/strncpy/strdup/memcpy/memset/memcmp/memchr
 *   - getopt_long (simple impl)
 *   - ctype: __ctype_b_loc / __ctype_tolower_loc
 *   - varargs: vfprintf via vsnprintf
 *
 * All I/O goes through OnyxOS syscalls (read/write/open/close/lseek).
 *
 * The shim is compiled and linked into the OnyxOS build of onyxcc.
 * Layout matches what the OnyxKernel loader expects: entry=_start at
 * CC_TEXT_VADDR=0x10000, R+X text segment, then R rodata, then R+W data+bss.
 */
#include "core/compat.h"

/* ---- OnyxOS syscalls ---- */
#define SYS_write 1
#define SYS_read  2
#define SYS_exit  3
#define SYS_yield 4
#define SYS_sbrk 13
#define SYS_open 8
#define SYS_close 9
#define SYS_lseek 10
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

static long _write(int fd, const void *buf, unsigned long n) {
    return __ecall4(SYS_write, fd, (long)buf, n);
}

static long _read(int fd, void *buf, unsigned long n) {
    return __ecall4(SYS_read, fd, (long)buf, n);
}

static void _exit(int code) {
    __ecall2(SYS_exit, code);
    for (;;) { }
}

static long _open(const char *path, int flags) {
    return __ecall4(SYS_open, (long)path, flags, 0666);
}

static long _close(int fd) {
    return __ecall2(SYS_close, fd);
}

static long _lseek(int fd, long off, int whence) {
    return __ecall4(SYS_lseek, fd, off, whence);
}

/* ---- Reusable single-threaded heap (16-byte payload alignment) ---- */
typedef struct heap_block heap_block_t;
struct heap_block {
    unsigned long size;
    heap_block_t *next;
    heap_block_t *previous;
    long available;
};
static heap_block_t *heap_first;
static heap_block_t *heap_last;

static void heap_merge(heap_block_t *b) {
    heap_block_t *next = b->next;
    if (!next || !next->available || (char *)(b + 1) + b->size != (char *)next) return;
    b->size += sizeof(heap_block_t) + next->size;
    b->next = next->next;
    if (b->next) b->next->previous = b;
    else heap_last = b;
}

static void heap_split(heap_block_t *b, unsigned long n) {
    if (b->size - n < sizeof(heap_block_t) + 16) return;
    heap_block_t *rest = (heap_block_t *)((char *)(b + 1) + n);
    rest->size = b->size - n - sizeof(heap_block_t);
    rest->next = b->next;
    rest->previous = b;
    rest->available = 1;
    if (rest->next) rest->next->previous = rest;
    else heap_last = rest;
    b->next = rest;
    b->size = n;
    heap_merge(rest);
}

void *malloc(unsigned long n) {
    if (n > 9223372036854775807UL - sizeof(heap_block_t) - 65551) return NULL;
    if (!n) n = 1;
    n = (n + 15) & ~15UL;
    heap_block_t *b = heap_first;
    while (b && (!b->available || b->size < n)) b = b->next;
    if (!b) {
        /* Arena chunks are 64 KiB plus a small header. Rounding every
         * such request to 128 KiB leaves a remainder too small for the
         * next arena chunk and nearly doubles compiler memory use. */
        unsigned long extent = (n + sizeof(heap_block_t) + 4095) & ~4095UL;
        if (extent < 65536) extent = 65536;
        long memory = __ecall2(SYS_sbrk, extent + 15);
        if (memory <= 0) return NULL;
        b = (heap_block_t *)(((unsigned long)memory + 15) & ~15UL);
        b->size = extent - sizeof(heap_block_t);
        b->next = NULL;
        b->previous = heap_last;
        b->available = 1;
        if (heap_last) heap_last->next = b;
        else heap_first = b;
        heap_last = b;
    }
    heap_split(b, n);
    b->available = 0;
    return b + 1;
}

void free(void *p) {
    if (!p) return;
    heap_block_t *b = (heap_block_t *)p - 1;
    b->available = 1;
    heap_merge(b);
    if (b->previous && b->previous->available) heap_merge(b->previous);
}

void *calloc(unsigned long n, unsigned long sz) {
    if (sz && n > (unsigned long)-1 / sz) return NULL;
    unsigned long total = n * sz;
    char *p = (char *)malloc(total);
    if (p) for (unsigned long i = 0; i < total; i++) p[i] = 0;
    return p;
}

void *realloc(void *p, unsigned long n) {
    if (!p) return malloc(n);
    if (!n) { free(p); return NULL; }
    if (n > 9223372036854775807UL - sizeof(heap_block_t) - 65551) return NULL;
    heap_block_t *b = (heap_block_t *)p - 1;
    unsigned long old_size = b->size;
    unsigned long rounded = (n + 15) & ~15UL;
    heap_merge(b);
    if (b->size >= rounded) { heap_split(b, rounded); return p; }
    char *q = (char *)malloc(n);
    if (!q) return NULL;
    for (unsigned long i = 0; i < old_size; i++) q[i] = ((char *)p)[i];
    free(p);
    return q;
}

void exit(int code) { _exit(code); }

/* ---- string.h ---- */
unsigned long strlen(const char *s) {
    const char *p = s;
    while (*p) p++;
    return (unsigned long)(p - s);
}

int strcmp(const char *a, const char *b) {
    for (;;) {
        unsigned char x = (unsigned char)*a++;
        unsigned char y = (unsigned char)*b++;
        if (x != y || !x) return (int)x - (int)y;
    }
}

int strncmp(const char *a, const char *b, unsigned long n) {
    for (unsigned long i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}

char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (c == 0) ? (char *)s : NULL;
}

char *strrchr(const char *s, int c) {
    char *last = NULL;
    while (*s) {
        if (*s == (char)c) last = (char *)s;
        s++;
    }
    if (c == 0) last = (char *)s;
    return last;
}

char *strstr(const char *h, const char *n) {
    if (!*n) return (char *)h;
    for (; *h; h++) {
        const char *p = h;
        const char *q = n;
        while (*p && *q && *p == *q) { p++; q++; }
        if (!*q) return (char *)h;
    }
    return NULL;
}

char *strncpy(char *d, const char *s, unsigned long n) {
    unsigned long i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}

char *strcpy(char *d, const char *s) {
    char *r = d;
    while ((*d++ = *s++)) ;
    return r;
}

char *strdup(const char *s) {
    unsigned long n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) {
        for (unsigned long i = 0; i < n; i++) p[i] = s[i];
    }
    return p;
}

/* may_alias keeps word operations valid on object representations when
 * building this freestanding shim with GCC/Clang as well as OnyxCC. */
typedef unsigned long shim_word_t __attribute__((__may_alias__));

void *memcpy(void *d, const void *s, unsigned long n) {
    char *dp = (char *)d;
    const char *sp = (const char *)s;
    while (n && ((unsigned long)dp & 7)) { *dp++ = *sp++; n--; }
    if (!n) return d;
    if (!((unsigned long)sp & 7)) {
        shim_word_t *dw = (shim_word_t *)dp;
        const shim_word_t *sw = (const shim_word_t *)sp;
        while (n >= 32) {
            dw[0] = sw[0]; dw[1] = sw[1]; dw[2] = sw[2]; dw[3] = sw[3];
            dw += 4; sw += 4; n -= 32;
        }
        while (n >= 8) { *dw++ = *sw++; n -= 8; }
        dp = (char *)dw;
        sp = (const char *)sw;
    }
    while (n) { *dp++ = *sp++; n--; }
    return d;
}

void *memset(void *d, int c, unsigned long n) {
    char *dp = (char *)d;
    while (n && ((unsigned long)dp & 7)) { *dp++ = (char)c; n--; }
    if (!n) return d;
    shim_word_t word = (unsigned char)c;
    word |= word << 8;
    word |= word << 16;
    word |= word << 32;
    shim_word_t *dw = (shim_word_t *)dp;
    while (n >= 32) {
        dw[0] = word; dw[1] = word; dw[2] = word; dw[3] = word;
        dw += 4; n -= 32;
    }
    while (n >= 8) { *dw++ = word; n -= 8; }
    dp = (char *)dw;
    while (n) { *dp++ = (char)c; n--; }
    return d;
}

int memcmp(const void *a, const void *b, unsigned long n) {
    const char *ap = (const char *)a;
    const char *bp = (const char *)b;
    for (unsigned long i = 0; i < n; i++) {
        if (ap[i] != bp[i]) return (int)(unsigned char)ap[i] - (int)(unsigned char)bp[i];
    }
    return 0;
}

void *memchr(const void *s, int c, unsigned long n) {
    const char *p = (const char *)s;
    for (unsigned long i = 0; i < n; i++) {
        if (p[i] == (char)c) return (void *)(p + i);
    }
    return NULL;
}

/* ---- ctype ---- */
/* OnyxCC uses __ctype_b_loc and __ctype_tolower_loc. We provide a small
 * table-based implementation. */
static const unsigned short _ctype_table[256] = {
    0,0,0,0,0,0,0,0,0,0x20,0x20,0x20,0x20,0x20,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0x20,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0x583,0x583,0x583,0x583,0x583,0x583,0x583,0x583,0x583,0x583,0,0,0,0,0,0,
    0,0x103,0x103,0x103,0x103,0x103,0x103,0x103,0x103,0x103,0x103,0x103,0x103,0x103,0x103,0x103,
    0x103,0x103,0x103,0x103,0x103,0x103,0x103,0x103,0x103,0x103,0x103,0,0,0,0,0,
    0,0x183,0x183,0x183,0x183,0x183,0x183,0x183,0x183,0x183,0x183,0x183,0x183,0x183,0x183,0x183,
    0x183,0x183,0x183,0x183,0x183,0x183,0x183,0x183,0x183,0x183,0x183,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

const unsigned short *_ctype_ptr;
unsigned short **__ctype_b_loc(void) {
    if (!_ctype_ptr) _ctype_ptr = _ctype_table;
    return (unsigned short **)&_ctype_ptr;
}

static int _tolower_table[256];
static int _tolower_init = 0;
static int *_tolower_ptr;
int **__ctype_tolower_loc(void) {
    if (!_tolower_ptr) _tolower_ptr = _tolower_table;
    if (!_tolower_init) {
        for (int i = 0; i < 256; i++) _tolower_table[i] = i;
        for (int i = 'A'; i <= 'Z'; i++) _tolower_table[i] = i + 32;
        _tolower_init = 1;
    }
    return (int **)&_tolower_ptr;
}

/* Direct ctype functions (used by OnyxCC sources). */
int isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isxdigit(int c){ return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int tolower(int c) { return isupper(c) ? c + 32 : c; }
int toupper(int c) { return islower(c) ? c - 32 : c; }
int isprint(int c) { return c >= 0x20 && c < 0x7f; }
int ispunct(int c) { return isprint(c) && !isspace(c) && !isalnum(c); }
int iscntrl(int c) { return c < 0x20 || c == 0x7f; }
int isgraph(int c) { return c > 0x20 && c < 0x7f; }

/* ---- stdio ---- */
#define MAX_FDS 16
static int _fd_table[MAX_FDS];
static int _fd_used[MAX_FDS];

typedef struct { int fd; } FILE;

static FILE _stderr_obj = { 2 };
FILE *stderr;
static FILE _stdout_obj = { 1 };
FILE *stdout;

FILE *fopen(const char *path, const char *mode) {
    if (!path || !mode || !mode[0]) return NULL;
    int flags;
    if (mode[0] == 'r') flags = 0;
    else if (mode[0] == 'w') flags = 1 | 64 | 512;
    else if (mode[0] == 'a') flags = 1 | 64 | 1024;
    else return NULL;
    if (strchr(mode, '+')) flags = (flags & ~3) | 2;
    int fd = (int)_open(path, flags);
    if (fd < 0) return NULL;
    FILE *f = (FILE *)malloc(sizeof(FILE));
    if (!f) { _close(fd); return NULL; }
    f->fd = fd;
    return f;
}

int fclose(FILE *f) {
    if (f) {
        _close(f->fd);
        free(f);
    }
    return 0;
}

unsigned long fread(void *buf, unsigned long sz, unsigned long n, FILE *f) {
    if (!sz || !n || n > (unsigned long)-1 / sz) return 0;
    unsigned long total = sz * n, done = 0;
    while (done < total) {
        long r = _read(f->fd, (char *)buf + done, total - done);
        if (r <= 0) break;
        done += (unsigned long)r;
    }
    return done / sz;
}

unsigned long fwrite(const void *buf, unsigned long sz, unsigned long n, FILE *f) {
    if (!sz || !n || n > (unsigned long)-1 / sz) return 0;
    unsigned long total = sz * n, done = 0;
    while (done < total) {
        long r = _write(f->fd, (const char *)buf + done, total - done);
        if (r <= 0) break;
        done += (unsigned long)r;
    }
    return done / sz;
}

int fseek(FILE *f, long off, int whence) {
    return _lseek(f->fd, off, whence) < 0 ? -1 : 0;
}

long ftell(FILE *f) {
    return _lseek(f->fd, 0, SEEK_CUR);
}

int fputc(int c, FILE *f) {
    char ch = (char)c;
    if (_write(f->fd, &ch, 1) != 1) return -1;
    return c;
}

int putchar(int c) {
    return fputc(c, &_stdout_obj);
}

/* ---- printf family ---- */
static int _vfmt(char *buf, int cap, const char *fmt, va_list ap) {
    int p = 0;
    while (*fmt && p < cap - 1) {
        if (*fmt != '%') { buf[p++] = *fmt++; continue; }
        fmt++;
        while (*fmt == '-' || *fmt == '+' || *fmt == ' ' || *fmt == '#' || *fmt == '0') fmt++;
        while (*fmt >= '0' && *fmt <= '9') fmt++;
        if (*fmt == '.') { fmt++; while (*fmt >= '0' && *fmt <= '9') fmt++; }
        int is_long = 0, is_longlong = 0;
        while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z' || *fmt == 'j' || *fmt == 't') {
            if (*fmt == 'l') { if (is_long) is_longlong = 1; is_long = 1; }
            fmt++;
        }
        char spec = *fmt++;
        switch (spec) {
            case 'd': case 'i': {
                long v = is_longlong ? va_arg(ap, long) : (long)va_arg(ap, int);
                int neg = 0;
                if (v < 0) { neg = 1; v = -v; }
                char tmp[24];
                int n = 0;
                if (v == 0) tmp[n++] = '0';
                while (v > 0) { tmp[n++] = '0' + (int)(v % 10); v /= 10; }
                if (neg && p < cap - 1) buf[p++] = '-';
                while (n > 0 && p < cap - 1) buf[p++] = tmp[--n];
                break;
            }
            case 'u': {
                unsigned long v = is_longlong ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
                char tmp[24];
                int n = 0;
                if (v == 0) tmp[n++] = '0';
                while (v > 0) { tmp[n++] = '0' + (int)(v % 10); v /= 10; }
                while (n > 0 && p < cap - 1) buf[p++] = tmp[--n];
                break;
            }
            case 'x': case 'X': {
                unsigned long v = is_longlong ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
                const char *set = (spec == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
                char tmp[24];
                int n = 0;
                if (v == 0) tmp[n++] = '0';
                while (v > 0) { tmp[n++] = set[v & 0xF]; v >>= 4; }
                while (n > 0 && p < cap - 1) buf[p++] = tmp[--n];
                break;
            }
            case 'c': {
                char c = (char)va_arg(ap, int);
                buf[p++] = c;
                break;
            }
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                while (*s && p < cap - 1) buf[p++] = *s++;
                break;
            }
            case 'p': {
                unsigned long v = (unsigned long)(unsigned long)va_arg(ap, void *);
                const char *set = "0123456789abcdef";
                if (p < cap - 1) buf[p++] = '0';
                if (p < cap - 1) buf[p++] = 'x';
                char tmp[24];
                int n = 0;
                while (v > 0) { tmp[n++] = set[v & 0xF]; v >>= 4; }
                if (n == 0) tmp[n++] = '0';
                while (n > 0 && p < cap - 1) buf[p++] = tmp[--n];
                break;
            }
            case '%': buf[p++] = '%'; break;
            default: buf[p++] = '%'; if (p < cap - 1) buf[p++] = spec; break;
        }
    }
    buf[p] = 0;
    return p;
}

int vsnprintf(char *buf, unsigned long cap, const char *fmt, va_list ap) {
    return _vfmt(buf, (int)cap, fmt, ap);
}

int snprintf(char *buf, unsigned long cap, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = _vfmt(buf, (int)cap, fmt, ap);
    va_end(ap);
    return n;
}

int vfprintf(FILE *f, const char *fmt, va_list ap) {
    char buf[2048];
    int n = _vfmt(buf, (int)sizeof(buf), fmt, ap);
    if (n > 0) _write(f->fd, buf, n);
    return n;
}

int fprintf(FILE *f, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vfprintf(f, fmt, ap);
    va_end(ap);
    return n;
}

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vfprintf(&_stdout_obj, fmt, ap);
    va_end(ap);
    return n;
}

/* ---- stdlib ---- */
char *getenv(const char *name) {
    /* OnyxOS doesn't have env in MVP. Return NULL. */
    (void)name;
    return NULL;
}

double strtod(const char *s, char **endp) {
    /* Minimal: parse integer part only. */
    double v = 0.0;
    int neg = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') {
        v = v * 10.0 + (*s - '0');
        s++;
    }
    if (*s == '.') {
        s++;
        double f = 0.1;
        while (*s >= '0' && *s <= '9') {
            v += (*s - '0') * f;
            f *= 0.1;
            s++;
        }
    }
    if (endp) *endp = (char *)s;
    return neg ? -v : v;
}

/* ---- getopt_long (simple impl) ---- */
struct option {
    const char *name;
    int has_arg;
    int *flag;
    int val;
};

#define no_argument 0
#define required_argument 1

char *optarg = NULL;
int optind = 1;
int optopt = 0;
int opterr = 1;

static int _optpos = 1;  /* position in current short opt group */

int getopt_long(int argc, char *const argv[], const char *optstring,
                const struct option *longopts, int *longindex) {
    if (optind >= argc) return -1;
    char *arg = argv[optind];
    if (arg[0] != '-' || arg[1] == 0) return -1;
    if (arg[1] == '-' && arg[2] == 0) { optind++; return -1; }

    /* Long option. */
    if (arg[1] == '-') {
        char *name = arg + 2;
        char *eq = strchr(name, '=');
        int nlen = eq ? (int)(eq - name) : (int)strlen(name);
        for (int i = 0; longopts[i].name; i++) {
            if ((int)strlen(longopts[i].name) == nlen &&
                strncmp(longopts[i].name, name, nlen) == 0) {
                optind++;
                if (longopts[i].has_arg) {
                    if (eq) optarg = eq + 1;
                    else if (optind < argc) optarg = argv[optind++];
                    else return '?';
                }
                if (longindex) *longindex = i;
                if (longopts[i].flag) { *longopts[i].flag = longopts[i].val; return 0; }
                return longopts[i].val;
            }
        }
        optind++;
        return '?';
    }

    /* Consume a whole short option before advancing optind. */
    char c = arg[_optpos++];
    const char *p = strchr(optstring, c);
    if (!p || c == ':') {
        optopt = c;
        if (!arg[_optpos]) { optind++; _optpos = 1; }
        return '?';
    }
    if (p[1] == ':') {
        if (arg[_optpos]) {
            optarg = arg + _optpos;
            optind++;
        } else if (optind + 1 < argc) {
            optarg = argv[optind + 1];
            optind += 2;
        } else {
            optind++;
            _optpos = 1;
            optopt = c;
            return '?';
        }
        _optpos = 1;
    } else if (!arg[_optpos]) {
        optind++;
        _optpos = 1;
    }
    return c;
}

/* ---- entry point ---- */
extern int main(int argc, char **argv);

/* Read argc (a0) and argv (a1) from RISC-V calling convention.
 * If argc == 0 (kernel didn't pass args), default to argc=1, argv={"onyxcc"} */
void _start(int argc, char **argv) {
    stderr = &_stderr_obj;
    stdout = &_stdout_obj;
    if (argc == 0) {
        static char *progname = "onyxcc";
        static char *default_argv[] = { NULL, NULL };
        default_argv[0] = progname;
        argc = 1;
        argv = default_argv;
    }
    int ret = main(argc, argv);
    _exit(ret);
}
