/*
 * stdio.c — libonyxc v0.5: buffered I/O, printf family, scanf family.
 *
 * What's new in v0.5:
 *   - FILE* streams with 4 KiB buffer (write/read coalescing).
 *   - fopen / fclose / fread / fwrite / fgets / fputs / fgetc / fputc
 *     / getc / putc / getchar / fputc / fseek / ftell / feof / ferror
 *     / rewind / clearerr / fileno.
 *   - sprintf / snprintf / vsnprintf / vsprintf — same format engine as
 *     printf, output to memory buffer instead of fd.
 *   - sscanf / vsscanf — covers %d, %ld, %lld, %u, %lu, %llu, %x, %lx,
 *     %o, %c, %s, %[], %p, and skip-white-space behavior.
 *   - perror — writes "[prog]: errno-message\n" to stderr.
 *   - getline / getdelim — POSIX line reader.
 *   - remove / rename — thin wrappers.
 *   - tmpfile / tmpnam — minimal in-memory /tmp/tmpfile placeholder.
 *
 * Not in v0.5 (future):
 *   - Wide-char functions (fgetwc, fputwc, etc.).
 *   - Binary vs text mode distinction (OnyxFS has no translation).
 *   - Locale-aware printf.
 */
#include "onyxc.h"
#include <errno.h>
#include <fcntl.h>

/* ── errno storage ──────────────────────────────────────────────────── */
static int g_errno = 0;
int *___errno_location(void) { return &g_errno; }

/* ── FILE stream structure ──────────────────────────────────────────── */
#define FILE_BUF_SIZE 4096
#define FILE_MAX      16

#define _IO_READ       0x0001
#define _IO_WRITE      0x0002
#define _IO_RDWR       0x0004
#define _IO_EOF        0x0008
#define _IO_ERR        0x0010
#define _IO_DIRTY      0x0020    /* write buffer has data */
#define _IO_UNREAD    0x0040    /* read buffer has data */
#define _IO_LINE_BUF  0x0080
#define _IO_IS_STDIO  0x0100

struct __FILE_s {
    int fd;
    int flags;
    long pos;          /* file position */
    char *buf;         /* buffer pointer */
    size_t buf_size;   /* buffer capacity */
    size_t buf_start;  /* read: start of valid data; write: 0 */
    size_t buf_end;    /* read: end of valid data; write: end of dirty data */
};

typedef struct __FILE_s FILE;

/* Pre-allocated FILE slots. */
static struct __FILE_s g_streams[FILE_MAX];
static char g_stream_bufs[FILE_MAX][FILE_BUF_SIZE];
static int g_streams_inited = 0;

static FILE g_stdin_obj;
static FILE g_stdout_obj;
static FILE g_stderr_obj;
static char g_stdin_buf[FILE_BUF_SIZE];
static char g_stdout_buf[FILE_BUF_SIZE];
static char g_stderr_buf[FILE_BUF_SIZE];

FILE *stdin  = &g_stdin_obj;
FILE *stdout = &g_stdout_obj;
FILE *stderr = &g_stderr_obj;

static void streams_init(void) {
    if (g_streams_inited) return;
    g_streams_inited = 1;
    for (int i = 0; i < FILE_MAX; i++) {
        g_streams[i].fd = -1;
        g_streams[i].flags = 0;
        g_streams[i].buf = g_stream_bufs[i];
        g_streams[i].buf_size = FILE_BUF_SIZE;
    }
    g_stdin_obj.fd = 0;
    g_stdin_obj.flags = _IO_READ | _IO_IS_STDIO;
    g_stdin_obj.buf = g_stdin_buf;
    g_stdin_obj.buf_size = FILE_BUF_SIZE;
    g_stdout_obj.fd = 1;
    g_stdout_obj.flags = _IO_WRITE | _IO_IS_STDIO | _IO_LINE_BUF;
    g_stdout_obj.buf = g_stdout_buf;
    g_stdout_obj.buf_size = FILE_BUF_SIZE;
    g_stderr_obj.fd = 2;
    g_stderr_obj.flags = _IO_WRITE | _IO_IS_STDIO | _IO_LINE_BUF;
    g_stderr_obj.buf = g_stderr_buf;
    g_stderr_obj.buf_size = FILE_BUF_SIZE;
}

/* ── format engine ──────────────────────────────────────────────────── */
typedef struct {
    char *buf;
    size_t pos;
    size_t cap;
    int    fd;        /* -1 = pure buffer; >=0 = write to fd */
    int    total;
    int    err;
} fmt_ctx_t;

static void fmt_emit(fmt_ctx_t *c, char ch) {
    if (c->err) return;
    if (c->buf) {
        if (c->pos < c->cap) {
            c->buf[c->pos++] = ch;
        } else {
            /* Buffer full — if we have an fd, flush and continue; else truncate. */
            if (c->fd >= 0) {
                long n = _onyx_write(c->fd, c->buf, c->pos);
                if (n < 0) { c->err = 1; return; }
                c->pos = 0;
                if (c->pos < c->cap) c->buf[c->pos++] = ch;
            } else {
                /* Pure-buffer truncation — caller checks cap. */
            }
        }
    } else if (c->fd >= 0) {
        char tmp[1] = { ch };
        long n = _onyx_write(c->fd, tmp, 1);
        if (n < 0) { c->err = 1; return; }
    }
    c->total++;
}

static void fmt_emit_str(fmt_ctx_t *c, const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) fmt_emit(c, s[i]);
}

static void fmt_emit_uint(fmt_ctx_t *c, unsigned long long v, int base, int upper) {
    char digits[32];
    int n = 0;
    const char *low = "0123456789abcdef";
    const char *up  = "0123456789ABCDEF";
    const char *set = upper ? up : low;
    if (v == 0) { fmt_emit(c, '0'); return; }
    while (v > 0 && n < (int)sizeof(digits)) {
        digits[n++] = set[v % base];
        v /= base;
    }
    while (n > 0) fmt_emit(c, digits[--n]);
}

static void fmt_emit_int(fmt_ctx_t *c, long long v) {
    if (v < 0) {
        fmt_emit(c, '-');
        v = -v;
    }
    fmt_emit_uint(c, (unsigned long long)v, 10, 0);
}

/* Specifier state. */
typedef struct {
    int width;        /* -1 = no width given */
    int precision;     /* -1 = no precision given */
    int is_long;      /* 0, 1 (l), 2 (ll) */
    int is_short;     /* 0, 1 (h), 2 (hh) */
    int is_size_t;    /* z */
    int is_ptrdiff;   /* t */
    int is_intmax;    /* j */
    int flag_minus;   /* left-align */
    int flag_plus;    /* force + */
    int flag_space;   /* leading space on positive */
    int flag_hash;    /* alternate form (0x, 0, etc.) */
    int flag_zero;    /* zero pad */
} fmt_spec_t;

static void fmt_emit_padded(fmt_ctx_t *c, const char *s, size_t n, const fmt_spec_t *sp, int is_num, char sign_char) {
    /* Compute padding. */
    int pad = 0;
    int content = (int)n + (sign_char ? 1 : 0);
    if (sp->width > content) pad = sp->width - content;

    /* For numbers with zero-pad and no left-align, replace spaces with zeros
     * after sign/prefix. */
    char pad_char = (is_num && sp->flag_zero && !sp->flag_minus) ? '0' : ' ';
    if (sp->flag_minus) pad_char = ' ';   /* left-aligned: always space */

    /* Sign / prefix first. */
    if (sign_char) fmt_emit(c, sign_char);

    if (sp->flag_minus) {
        fmt_emit_str(c, s, n);
        while (pad-- > 0) fmt_emit(c, ' ');
    } else {
        while (pad-- > 0) fmt_emit(c, pad_char);
        fmt_emit_str(c, s, n);
    }
}

static void format_str(fmt_ctx_t *c, const fmt_spec_t *sp, const char *s) {
    if (!s) s = "(null)";
    size_t n = 0;
    while (s[n]) n++;
    if (sp->precision >= 0 && (int)n > sp->precision) n = (size_t)sp->precision;
    fmt_emit_padded(c, s, n, sp, 0, 0);
}

static void format_int(fmt_ctx_t *c, const fmt_spec_t *sp, long long v, int base, int upper) {
    char buf[32];
    int n = 0;
    int neg = 0;
    unsigned long long u;
    const char *low = "0123456789abcdef";
    const char *up  = "0123456789ABCDEF";
    const char *set = upper ? up : low;

    if (v < 0 && base == 10) {
        neg = 1;
        u = (unsigned long long)(-v);
    } else if (v < 0) {
        /* For hex/octal, treat negative as its unsigned bit pattern. */
        u = (unsigned long long)v;
    } else {
        u = (unsigned long long)v;
    }

    if (u == 0) {
        buf[n++] = '0';
    } else {
        while (u > 0 && n < (int)sizeof(buf)) {
            buf[n++] = set[u % base];
            u /= base;
        }
    }

    /* Apply precision: minimum number of digits. */
    if (sp->precision > 0 && sp->precision > n) {
        int extra = sp->precision - n;
        /* Shift digits right and fill with '0' on the left. */
        for (int i = n - 1; i >= 0; i--) buf[i + extra] = buf[i];
        for (int i = 0; i < extra; i++) buf[i] = '0';
        n += extra;
    }

    /* Reverse the buffer (digits are currently least-significant first). */
    for (int i = 0; i < n / 2; i++) {
        char t = buf[i]; buf[i] = buf[n - 1 - i]; buf[n - 1 - i] = t;
    }

    char sign = 0;
    if (base == 10) {
        if (neg) sign = '-';
        else if (sp->flag_plus) sign = '+';
        else if (sp->flag_space) sign = ' ';
    }

    /* "0x"/"0" prefix for hash with %x/%o. */
    char prefix[3] = {0,0,0};
    int prefix_len = 0;
    if (sp->flag_hash) {
        if (base == 16 && v != 0) {
            prefix[0] = '0';
            prefix[1] = upper ? 'X' : 'x';
            prefix_len = 2;
        } else if (base == 8 && v != 0 && buf[0] != '0') {
            prefix[0] = '0';
            prefix_len = 1;
        }
    }

    /* Build the full content string. */
    char full[40];
    int fn = 0;
    for (int i = 0; i < prefix_len; i++) full[fn++] = prefix[i];
    for (int i = 0; i < n; i++) full[fn++] = buf[i];

    /* For zero-padding with sign/prefix, the sign and prefix come first. */
    if (sp->flag_zero && !sp->flag_minus) {
        if (sign) fmt_emit(c, sign);
        for (int i = 0; i < prefix_len; i++) fmt_emit(c, prefix[i]);
        int pad = sp->width - fn - (sign ? 1 : 0);
        while (pad-- > 0) fmt_emit(c, '0');
        for (int i = 0; i < n; i++) fmt_emit(c, buf[i]);
    } else {
        fmt_emit_padded(c, full, fn, sp, 1, sign);
    }
}

/* ── public v*printf entry points ────────────────────────────────────── */
static int vformat_run(fmt_ctx_t *c, const char *fmt, va_list ap) {
    while (*fmt) {
        if (*fmt != '%') { fmt_emit(c, *fmt++); continue; }
        fmt++;   /* skip % */

        fmt_spec_t sp;
        memset(&sp, 0, sizeof(sp));
        sp.width = -1;
        sp.precision = -1;

        /* Flags. */
        for (;;) {
            if (*fmt == '-') { sp.flag_minus = 1; fmt++; }
            else if (*fmt == '+') { sp.flag_plus = 1; fmt++; }
            else if (*fmt == ' ') { sp.flag_space = 1; fmt++; }
            else if (*fmt == '#') { sp.flag_hash = 1; fmt++; }
            else if (*fmt == '0') { sp.flag_zero = 1; fmt++; }
            else break;
        }

        /* Width — number or "*". */
        if (*fmt == '*') {
            int w = va_arg(ap, int);
            if (w < 0) { sp.flag_minus = 1; w = -w; }
            sp.width = w;
            fmt++;
        } else {
            int w = 0;
            while (*fmt >= '0' && *fmt <= '9') { w = w * 10 + (*fmt - '0'); fmt++; }
            if (w > 0) sp.width = w;
        }

        /* Precision. */
        if (*fmt == '.') {
            fmt++;
            if (*fmt == '*') {
                int p = va_arg(ap, int);
                sp.precision = p < 0 ? -1 : p;
                fmt++;
            } else {
                int p = 0;
                while (*fmt >= '0' && *fmt <= '9') { p = p * 10 + (*fmt - '0'); fmt++; }
                sp.precision = p;
            }
        }

        /* Length modifiers. */
        for (;;) {
            if (*fmt == 'l') {
                if (sp.is_long) sp.is_long = 2; else sp.is_long = 1;
                fmt++;
            } else if (*fmt == 'h') {
                if (sp.is_short) sp.is_short = 2; else sp.is_short = 1;
                fmt++;
            } else if (*fmt == 'z') { sp.is_size_t = 1; fmt++; }
            else if (*fmt == 'j') { sp.is_intmax = 1; fmt++; }
            else if (*fmt == 't') { sp.is_ptrdiff = 1; fmt++; }
            else if (*fmt == 'L') { fmt++; }   /* long double, treated as double */
            else break;
        }

        char spec = *fmt++;
        switch (spec) {
            case 'd': case 'i': {
                long long v;
                if (sp.is_long == 2 || sp.is_intmax) v = va_arg(ap, long long);
                else if (sp.is_long == 1 || sp.is_size_t || sp.is_ptrdiff) v = va_arg(ap, long);
                else v = va_arg(ap, int);
                format_int(c, &sp, v, 10, 0);
                break;
            }
            case 'u': {
                unsigned long long v;
                if (sp.is_long == 2 || sp.is_intmax) v = va_arg(ap, unsigned long long);
                else if (sp.is_long == 1 || sp.is_size_t || sp.is_ptrdiff) v = va_arg(ap, unsigned long);
                else v = va_arg(ap, unsigned int);
                format_int(c, &sp, (long long)v, 10, 0);
                break;
            }
            case 'x': {
                unsigned long long v;
                if (sp.is_long == 2 || sp.is_intmax) v = va_arg(ap, unsigned long long);
                else if (sp.is_long == 1 || sp.is_size_t || sp.is_ptrdiff) v = va_arg(ap, unsigned long);
                else v = va_arg(ap, unsigned int);
                format_int(c, &sp, (long long)v, 16, 0);
                break;
            }
            case 'X': {
                unsigned long long v;
                if (sp.is_long == 2 || sp.is_intmax) v = va_arg(ap, unsigned long long);
                else if (sp.is_long == 1 || sp.is_size_t || sp.is_ptrdiff) v = va_arg(ap, unsigned long);
                else v = va_arg(ap, unsigned int);
                format_int(c, &sp, (long long)v, 16, 1);
                break;
            }
            case 'o': {
                unsigned long long v;
                if (sp.is_long == 2 || sp.is_intmax) v = va_arg(ap, unsigned long long);
                else if (sp.is_long == 1 || sp.is_size_t || sp.is_ptrdiff) v = va_arg(ap, unsigned long);
                else v = va_arg(ap, unsigned int);
                format_int(c, &sp, (long long)v, 8, 0);
                break;
            }
            case 'c': {
                char ch = (char)va_arg(ap, int);
                fmt_emit_padded(c, &ch, 1, &sp, 0, 0);
                break;
            }
            case 's': {
                const char *s = va_arg(ap, const char *);
                format_str(c, &sp, s);
                break;
            }
            case 'p': {
                void *p = va_arg(ap, void *);
                unsigned long long v = (unsigned long long)(size_t)p;
                /* Always print with "0x" prefix, no zero-pad by default. */
                char buf[16];
                int n = 0;
                if (v == 0) { buf[n++] = '0'; }
                while (v > 0 && n < (int)sizeof(buf)) {
                    int d = (int)(v & 0xF);
                    buf[n++] = d < 10 ? '0' + d : 'a' + d - 10;
                    v >>= 4;
                }
                /* Reverse. */
                for (int i = 0; i < n / 2; i++) {
                    char t = buf[i]; buf[i] = buf[n - 1 - i]; buf[n - 1 - i] = t;
                }
                fmt_emit(c, '0'); fmt_emit(c, 'x');
                fmt_emit_str(c, buf, n);
                break;
            }
            case 'n': {
                /* Store number of chars written so far. */
                int *p = va_arg(ap, int *);
                if (p) *p = c->total;
                break;
            }
            case '%': fmt_emit(c, '%'); break;
            case 'f': case 'F': case 'g': case 'G': case 'e': case 'E': {
                /* Limited float support: print integer part + fractional part
                 * to precision digits. Truncated, no rounding. */
                double d = va_arg(ap, double);
                if (d < 0) { fmt_emit(c, '-'); d = -d; }
                long long ip = (long long)d;
                double fp = d - (double)ip;
                int prec0 = sp.precision >= 0 ? sp.precision : 6;
                if (prec0 > 0) {
                    /* Round first, then print the (possibly carried) integer
                     * part — avoids 2.5 printing as 2.49. */
                    double scale = 1.0;
                    for (int i = 0; i < prec0; i++) scale *= 10.0;
                    double fr = (d - (double)ip) * scale;
                    long long frac = (long long)(fr + 0.5);
                    if (frac >= (long long)scale) {
                        ip += 1;
                        frac -= (long long)scale;
                    }
                    fmt_emit_uint(c, (unsigned long long)ip, 10, 0);
                    fmt_emit(c, '.');
                    char digs[24];
                    int nd = 0;
                    for (int i = 0; i < prec0; i++) {
                        digs[nd++] = (char)('0' + (frac % 10));
                        frac /= 10;
                    }
                    while (nd > 0) fmt_emit(c, digs[--nd]);
                } else {
                    fmt_emit_uint(c, (unsigned long long)ip, 10, 0);
                }
                break;
                break;
            }
            default:
                fmt_emit(c, '%');
                fmt_emit(c, spec);
                break;
        }
    }
    return c->total;
}

/* Public: write to fd, with optional buffer. */
int vfprintf(FILE *fp, const char *fmt, va_list ap) {
    streams_init();
    if (!fp) fp = stdout;
    fmt_ctx_t c;
    c.buf = fp->buf;
    c.pos = fp->buf_end;
    c.cap = fp->buf_size;
    c.fd = (fp->flags & _IO_WRITE) ? fp->fd : -1;
    c.total = 0;
    c.err = 0;
    int total = vformat_run(&c, fmt, ap);
    /* Flush remaining buffer to fd. */
    if (c.pos > 0 && c.fd >= 0) {
        long n = _onyx_write(c.fd, fp->buf, c.pos);
        if (n < 0) c.err = 1;
    }
    fp->buf_end = 0;   /* reset write buffer */
    return c.err ? -1 : total;
}

int vprintf(const char *fmt, va_list ap) {
    return vfprintf(stdout, fmt, ap);
}

int vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap) {
    fmt_ctx_t c;
    c.buf = buf;
    c.pos = 0;
    c.cap = cap;
    c.fd = -1;
    c.total = 0;
    c.err = 0;
    int total = vformat_run(&c, fmt, ap);
    /* Always null-terminate. */
    if (cap > 0) {
        buf[(c.pos < cap) ? c.pos : cap - 1] = '\0';
    }
    (void)total;
    return c.total;
}

int vsprintf(char *buf, const char *fmt, va_list ap) {
    return vsnprintf(buf, (size_t)-1, fmt, ap);
}

int snprintf(char *buf, size_t cap, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, cap, fmt, ap);
    va_end(ap);
    return n;
}

int sprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsprintf(buf, fmt, ap);
    va_end(ap);
    return n;
}

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}

int fprintf(FILE *fp, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vfprintf(fp, fmt, ap);
    va_end(ap);
    return n;
}

/* ── scanf family ────────────────────────────────────────────────────── */
typedef struct {
    const char *p;
    int nmatched;
    int eof;
} scan_ctx_t;

static int scan_skip_ws(scan_ctx_t *c) {
    int ch;
    while ((ch = *c->p) != 0) {
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\v' || ch == '\f') {
            c->p++;
        } else {
            return ch;
        }
    }
    c->eof = 1;
    return 0;
}

static int scan_next_char(scan_ctx_t *c) {
    int ch = *c->p;
    if (ch == 0) { c->eof = 1; return -1; }
    c->p++;
    return ch;
}

static int scan_peek(scan_ctx_t *c) {
    if (*c->p == 0) return -1;
    return *c->p;
}

static void scan_putback(scan_ctx_t *c, char ch) {
    /* We don't really unread — we re-insert the char by going back one.
     * This only works because c->p points into the user-supplied buffer. */
    c->p--;
    *(char *)c->p = (char)ch;
}

static long long scan_int(scan_ctx_t *c, int base, int *consumed_any) {
    int sign = 1;
    long long v = 0;
    int any = 0;
    scan_skip_ws(c);
    int ch = scan_peek(c);
    if (ch == '-') { sign = -1; scan_next_char(c); ch = scan_peek(c); }
    else if (ch == '+') { scan_next_char(c); ch = scan_peek(c); }

    if (base == 16 && ch == '0') {
        scan_next_char(c);
        ch = scan_peek(c);
        if (ch == 'x' || ch == 'X') { scan_next_char(c); ch = scan_peek(c); }
    } else if (base == 0) {
        if (ch == '0') {
            scan_next_char(c);
            ch = scan_peek(c);
            if (ch == 'x' || ch == 'X') { scan_next_char(c); base = 16; }
            else { base = 8; any = 1; }
        } else {
            base = 10;
        }
    }

    while (ch != -1) {
        int d;
        if (ch >= '0' && ch <= '9') d = ch - '0';
        else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * base + d;
        scan_next_char(c);
        ch = scan_peek(c);
        any = 1;
    }
    *consumed_any = any;
    return v * sign;
}

int vsscanf(const char *buf, const char *fmt, va_list ap) {
    scan_ctx_t c;
    c.p = buf;
    c.nmatched = 0;
    c.eof = 0;

    while (*fmt) {
        if (*fmt == ' ' || *fmt == '\t' || *fmt == '\n') {
            fmt++;
            scan_skip_ws(&c);
            continue;
        }
        if (*fmt != '%') {
            int ch = scan_next_char(&c);
            if (ch != *fmt) return c.nmatched;
            fmt++;
            continue;
        }
        fmt++;
        if (*fmt == '%') {
            int ch = scan_next_char(&c);
            if (ch != '%') return c.nmatched;
            fmt++;
            continue;
        }

        /* Suppress store if "*" present. */
        int suppress = 0;
        if (*fmt == '*') { suppress = 1; fmt++; }
        int width = -1;
        if (*fmt >= '0' && *fmt <= '9') {
            width = 0;
            while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        }
        (void)width;
        int is_long = 0, is_short = 0, is_size_t = 0;
        for (;;) {
            if (*fmt == 'l') { if (is_long) is_long = 2; else is_long = 1; fmt++; }
            else if (*fmt == 'h') { if (is_short) is_short = 2; else is_short = 1; fmt++; }
            else if (*fmt == 'z') { is_size_t = 1; fmt++; }
            else if (*fmt == 'j') { is_long = 2; fmt++; }
            else if (*fmt == 't') { is_long = 1; fmt++; }
            else break;
        }

        char spec = *fmt++;
        switch (spec) {
            case 'd': case 'i': {
                int base = (spec == 'i') ? 0 : 10;
                int any = 0;
                long long v = scan_int(&c, base, &any);
                if (!any) return c.nmatched;
                if (!suppress) {
                    if (is_long == 2) *va_arg(ap, long long *) = (long long)v;
                    else if (is_long == 1) *va_arg(ap, long *) = (long)v;
                    else if (is_short) *va_arg(ap, short *) = (short)v;
                    else *va_arg(ap, int *) = (int)v;
                    c.nmatched++;
                }
                break;
            }
            case 'u': {
                int any = 0;
                unsigned long long v = (unsigned long long)scan_int(&c, 10, &any);
                if (!any) return c.nmatched;
                if (!suppress) {
                    if (is_long == 2) *va_arg(ap, unsigned long long *) = (unsigned long long)v;
                    else if (is_long == 1 || is_size_t) *va_arg(ap, unsigned long *) = (unsigned long)v;
                    else if (is_short) *va_arg(ap, unsigned short *) = (unsigned short)v;
                    else *va_arg(ap, unsigned int *) = (unsigned int)v;
                    c.nmatched++;
                }
                break;
            }
            case 'x': case 'X': case 'p': {
                int any = 0;
                long long v = scan_int(&c, 16, &any);
                if (!any) return c.nmatched;
                if (!suppress) {
                    if (is_long == 2) *va_arg(ap, unsigned long long *) = (unsigned long long)v;
                    else if (is_long == 1) *va_arg(ap, unsigned long *) = (unsigned long)v;
                    else *va_arg(ap, unsigned int *) = (unsigned int)v;
                    c.nmatched++;
                }
                break;
            }
            case 'o': {
                int any = 0;
                long long v = scan_int(&c, 8, &any);
                if (!any) return c.nmatched;
                if (!suppress) {
                    if (is_long == 2) *va_arg(ap, unsigned long long *) = (unsigned long long)v;
                    else if (is_long == 1) *va_arg(ap, unsigned long *) = (unsigned long)v;
                    else *va_arg(ap, unsigned int *) = (unsigned int)v;
                    c.nmatched++;
                }
                break;
            }
            case 's': {
                scan_skip_ws(&c);
                char *out = suppress ? NULL : va_arg(ap, char *);
                int n = 0;
                int ch;
                while ((ch = scan_peek(&c)) != -1 && ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r' && ch != '\v' && ch != '\f') {
                    if (out) out[n] = (char)ch;
                    n++;
                    scan_next_char(&c);
                }
                if (out) out[n] = 0;
                if (n == 0) return c.nmatched;
                if (!suppress) c.nmatched++;
                break;
            }
            case 'c': {
                int ch = scan_next_char(&c);
                if (ch == -1) return c.nmatched;
                if (!suppress) {
                    char *p = va_arg(ap, char *);
                    *p = (char)ch;
                    c.nmatched++;
                }
                break;
            }
            case '[': {
                /* Character class. */
                int negate = 0;
                if (*fmt == '^') { negate = 1; fmt++; }
                char set[256];
                memset(set, 0, sizeof(set));
                if (*fmt == ']') { set[(int)']'] = 1; fmt++; }
                while (*fmt && *fmt != ']') {
                    set[(int)(unsigned char)*fmt] = 1;
                    fmt++;
                }
                if (*fmt == ']') fmt++;
                char *out = suppress ? NULL : va_arg(ap, char *);
                int n = 0;
                int ch;
                while ((ch = scan_peek(&c)) != -1) {
                    int in_set = set[ch];
                    if (negate ? in_set : !in_set) break;
                    if (out) out[n] = (char)ch;
                    n++;
                    scan_next_char(&c);
                }
                if (out) out[n] = 0;
                if (n == 0) return c.nmatched;
                if (!suppress) c.nmatched++;
                break;
            }
            case 'n': {
                if (!suppress) {
                    if (is_long == 2) *va_arg(ap, long long *) = (long long)(c.p - buf);
                    else if (is_long == 1) *va_arg(ap, long *) = (long)(c.p - buf);
                    else *va_arg(ap, int *) = (int)(c.p - buf);
                }
                break;
            }
            default:
                /* Unknown specifier — stop scanning. */
                return c.nmatched;
        }
    }
    return c.nmatched;
}

int sscanf(const char *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsscanf(buf, fmt, ap);
    va_end(ap);
    return n;
}

int fscanf(FILE *fp, const char *fmt, ...) {
    /* Read up to 4 KiB from the stream into a temp buffer, then sscanf it. */
    static char tmpbuf[4096];
    size_t n = fread(tmpbuf, 1, sizeof(tmpbuf) - 1, fp);
    tmpbuf[n] = 0;
    va_list ap;
    va_start(ap, fmt);
    int r = vsscanf(tmpbuf, fmt, ap);
    va_end(ap);
    return r;
}

int scanf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vfscanf(stdin, fmt, ap);
    va_end(ap);
    return n;
}

int vfscanf(FILE *fp, const char *fmt, va_list ap) {
    static char tmpbuf[4096];
    size_t n = fread(tmpbuf, 1, sizeof(tmpbuf) - 1, fp);
    tmpbuf[n] = 0;
    return vsscanf(tmpbuf, fmt, ap);
}

/* ── FILE* I/O ───────────────────────────────────────────────────────── */
static int file_flush_locked(FILE *fp) {
    if (!(fp->flags & _IO_WRITE)) return 0;
    if (fp->buf_end == 0) return 0;
    long n = _onyx_write(fp->fd, fp->buf, fp->buf_end);
    if (n < 0) {
        fp->flags |= _IO_ERR;
        errno = (int)(-n);
        return -1;
    }
    fp->pos += n;
    fp->buf_end = 0;
    return 0;
}

static int file_fill_locked(FILE *fp) {
    if (!(fp->flags & _IO_READ)) return -1;
    long n = _onyx_read(fp->fd, fp->buf, fp->buf_size);
    if (n < 0) {
        fp->flags |= _IO_ERR;
        errno = (int)(-n);
        return -1;
    }
    if (n == 0) {
        fp->flags |= _IO_EOF;
        return 0;
    }
    fp->buf_start = 0;
    fp->buf_end = (size_t)n;
    return (int)n;
}

FILE *fopen(const char *path, const char *mode) {
    streams_init();
    int flags = 0;
    int open_flags = 0;
    int open_mode = 0644;
    /* Parse mode. */
    if (mode[0] == 'r') {
        flags = _IO_READ;
        open_flags = O_RDONLY;
        if (mode[1] == '+') { flags = _IO_RDWR; open_flags = O_RDWR; }
    } else if (mode[0] == 'w') {
        flags = _IO_WRITE;
        open_flags = O_WRONLY | O_CREAT | O_TRUNC;
        if (mode[1] == '+') { flags = _IO_RDWR; open_flags = O_RDWR | O_CREAT | O_TRUNC; }
    } else if (mode[0] == 'a') {
        flags = _IO_WRITE;
        open_flags = O_WRONLY | O_CREAT | O_APPEND;
        if (mode[1] == '+') { flags = _IO_RDWR; open_flags = O_RDWR | O_CREAT | O_APPEND; }
    } else {
        errno = EINVAL;
        return NULL;
    }
    long fd = _onyx_open(path, open_flags, open_mode);
    if (fd < 0) {
        errno = (int)(-fd);
        return NULL;
    }
    /* Find a free slot. */
    for (int i = 0; i < FILE_MAX; i++) {
        if (g_streams[i].fd == -1) {
            g_streams[i].fd = (int)fd;
            g_streams[i].flags = flags;
            g_streams[i].pos = 0;
            g_streams[i].buf_start = 0;
            g_streams[i].buf_end = 0;
            return &g_streams[i];
        }
    }
    _onyx_close((int)fd);
    errno = EMFILE;
    return NULL;
}

int fclose(FILE *fp) {
    if (!fp) { errno = EBADF; return -1; }
    if (fp->flags & _IO_IS_STDIO) {
        /* Don't actually close std streams. */
        file_flush_locked(fp);
        return 0;
    }
    file_flush_locked(fp);
    _onyx_close(fp->fd);
    fp->fd = -1;
    fp->flags = 0;
    return 0;
}

size_t fread(void *buf, size_t sz, size_t count, FILE *fp) {
    if (!fp || !buf) { errno = EFAULT; return 0; }
    size_t want = sz * count;
    size_t got = 0;
    char *out = (char *)buf;
    while (got < want) {
        if (fp->buf_start >= fp->buf_end) {
            int n = file_fill_locked(fp);
            if (n <= 0) break;
        }
        size_t avail = fp->buf_end - fp->buf_start;
        size_t need = want - got;
        size_t take = avail < need ? avail : need;
        memcpy(out + got, fp->buf + fp->buf_start, take);
        fp->buf_start += take;
        got += take;
    }
    return got / sz;
}

size_t fwrite(const void *buf, size_t sz, size_t count, FILE *fp) {
    if (!fp || !buf) { errno = EFAULT; return 0; }
    size_t want = sz * count;
    const char *in = (const char *)buf;
    size_t put = 0;
    while (put < want) {
        if (fp->buf_end >= fp->buf_size) {
            if (file_flush_locked(fp) < 0) return put / sz;
        }
        size_t avail = fp->buf_size - fp->buf_end;
        size_t need = want - put;
        size_t take = avail < need ? avail : need;
        memcpy(fp->buf + fp->buf_end, in + put, take);
        fp->buf_end += take;
        put += take;
        if (fp->flags & _IO_LINE_BUF) {
            /* Line-buffered: flush on newline. */
            for (size_t i = fp->buf_end - take; i < fp->buf_end; i++) {
                if (fp->buf[i] == '\n') {
                    file_flush_locked(fp);
                    break;
                }
            }
        }
    }
    return put / sz;
}

int fgetc(FILE *fp) {
    if (!fp) return -1;
    if (fp->buf_start >= fp->buf_end) {
        if (file_fill_locked(fp) <= 0) return -1;
    }
    return (unsigned char)fp->buf[fp->buf_start++];
}

int getc(FILE *fp) { return fgetc(fp); }
int getchar(void) { return fgetc(stdin); }

int fputc(int ch, FILE *fp) {
    if (!fp) return -1;
    char c = (char)ch;
    if (fwrite(&c, 1, 1, fp) != 1) return -1;
    return (unsigned char)ch;
}

int putc(int ch, FILE *fp) { return fputc(ch, fp); }
int putchar(int ch) { return fputc(ch, stdout); }

int fputs(const char *s, FILE *fp) {
    if (!s || !fp) return -1;
    size_t n = strlen(s);
    if (fwrite(s, 1, n, fp) != n) return -1;
    return 0;
}

int puts(const char *s) {
    if (fputs(s, stdout) < 0) return -1;
    if (fputc('\n', stdout) < 0) return -1;
    return (int)strlen(s) + 1;
}

char *fgets(char *buf, int size, FILE *fp) {
    if (!buf || !fp || size <= 0) return NULL;
    int n = 0;
    while (n < size - 1) {
        int ch = fgetc(fp);
        if (ch == -1) {
            if (n == 0) return NULL;
            break;
        }
        buf[n++] = (char)ch;
        if (ch == '\n') break;
    }
    buf[n] = 0;
    return buf;
}

long getline(char **lineptr, size_t *n, FILE *fp) {
    if (!lineptr || !n || !fp) { errno = EINVAL; return -1; }
    if (!*lineptr || *n == 0) {
        *n = 128;
        *lineptr = malloc(*n);
        if (!*lineptr) { *n = 0; return -1; }
    }
    size_t len = 0;
    int ch;
    while ((ch = fgetc(fp)) != -1) {
        if (len + 2 > *n) {
            size_t newn = *n * 2;
            char *p = realloc(*lineptr, newn);
            if (!p) return -1;
            *lineptr = p;
            *n = newn;
        }
        (*lineptr)[len++] = (char)ch;
        if (ch == '\n') break;
    }
    if (len == 0) return -1;
    (*lineptr)[len] = 0;
    return (long)len;
}

long getdelim(char **lineptr, size_t *n, int delim, FILE *fp) {
    if (!lineptr || !n || !fp) { errno = EINVAL; return -1; }
    if (!*lineptr || *n == 0) {
        *n = 128;
        *lineptr = malloc(*n);
        if (!*lineptr) { *n = 0; return -1; }
    }
    size_t len = 0;
    int ch;
    while ((ch = fgetc(fp)) != -1) {
        if (len + 2 > *n) {
            size_t newn = *n * 2;
            char *p = realloc(*lineptr, newn);
            if (!p) return -1;
            *lineptr = p;
            *n = newn;
        }
        (*lineptr)[len++] = (char)ch;
        if (ch == delim) break;
    }
    if (len == 0) return -1;
    (*lineptr)[len] = 0;
    return (long)len;
}

int fseek(FILE *fp, long offset, int whence) {
    if (!fp) { errno = EBADF; return -1; }
    file_flush_locked(fp);
    long r = _onyx_lseek(fp->fd, offset, whence);
    if (r < 0) { errno = (int)(-r); return -1; }
    fp->pos = r;
    fp->buf_start = fp->buf_end = 0;
    fp->flags &= ~_IO_EOF;
    return 0;
}

long ftell(FILE *fp) {
    if (!fp) return -1;
    long adjust = 0;
    if (fp->flags & _IO_READ) {
        adjust = (long)(fp->buf_end - fp->buf_start);
    } else if (fp->flags & _IO_WRITE) {
        adjust = (long)fp->buf_end;
    }
    return fp->pos - adjust;
}

int feof(FILE *fp) { return fp && (fp->flags & _IO_EOF) ? 1 : 0; }
int ferror(FILE *fp) { return fp && (fp->flags & _IO_ERR) ? 1 : 0; }
void clearerr(FILE *fp) { if (fp) fp->flags &= ~(_IO_EOF | _IO_ERR); }
void rewind(FILE *fp) { fseek(fp, 0, SEEK_SET); if (fp) fp->flags &= ~_IO_ERR; }
int fileno(FILE *fp) { return fp ? fp->fd : -1; }

int fflush(FILE *fp) {
    if (!fp) {
        /* Flush all open streams. */
        for (int i = 0; i < FILE_MAX; i++) {
            if (g_streams[i].fd != -1) file_flush_locked(&g_streams[i]);
        }
        file_flush_locked(stdout);
        file_flush_locked(stderr);
        return 0;
    }
    return file_flush_locked(fp);
}

int remove(const char *path) {
    long r = _onyx_unlink(path);
    if (r < 0) { errno = (int)(-r); return -1; }
    return 0;
}

int rename(const char *oldp, const char *newp) {
    long r = _onyx_rename(oldp, newp);
    if (r < 0) { errno = (int)(-r); return -1; }
    return 0;
}

FILE *tmpfile(void) {
    static int counter = 0;
    char name[] = "/tmp/tmpXXXXXX";
    int pid = (int)_onyx_getpid();
    for (int i = 0; i < 6; i++) {
        name[9 + i] = 'A' + ((pid ^ counter ^ (i * 13)) % 26);
    }
    counter++;
    return fopen(name, "w+");
}

char *tmpnam(char *buf) {
    static char internal[32];
    static int counter = 0;
    char *out = buf ? buf : internal;
    int pid = (int)_onyx_getpid();
    int n = 0;
    const char *prefix = "/tmp/tmp";
    while (prefix[n]) { out[n] = prefix[n]; n++; }
    for (int i = 0; i < 6; i++) {
        out[n++] = 'A' + ((pid ^ counter ^ (i * 7)) % 26);
    }
    out[n] = 0;
    counter++;
    return out;
}
