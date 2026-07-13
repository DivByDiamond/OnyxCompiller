/*
 * stdio.c — minimal printf family.
 *
 * Supports:
 *   %d, %i    signed int
 *   %u        unsigned int
 *   %x, %X    hex
 *   %o        octal
 *   %c        char
 *   %s        string
 *   %p        pointer (0x prefixed hex)
 *   %%        literal %
 *   %ld, %lu, %lx, %lld, %llu, %llx  — long variants
 *   %hhd, %hu — short variants (treated as int)
 * Width and precision are NOT supported in MVP.
 */
#include "onyxc.h"

#define STDOUT_FD 1
#define STDERR_FD 2

static int write_str(int fd, const char *s, size_t n) {
    return (int)_onyx_write(fd, s, n);
}

static int put_buf(char *buf, int *pos, int cap, char c) {
    if (*pos < cap - 1) {
        buf[(*pos)++] = c;
        return 1;
    }
    return 0;
}

static int put_str(char *buf, int *pos, int cap, const char *s) {
    int n = 0;
    while (*s) {
        n += put_buf(buf, pos, cap, *s++);
    }
    return n;
}

static int put_uint(char *buf, int *pos, int cap, unsigned long long v, int base, int upper) {
    char digits[32];
    int n = 0;
    const char *low = "0123456789abcdef";
    const char *up  = "0123456789ABCDEF";
    const char *set = upper ? up : low;
    if (v == 0) {
        return put_buf(buf, pos, cap, '0');
    }
    while (v > 0 && n < (int)sizeof(digits)) {
        digits[n++] = set[v % base];
        v /= base;
    }
    int written = 0;
    while (n > 0) {
        written += put_buf(buf, pos, cap, digits[--n]);
    }
    return written;
}

static int put_int(char *buf, int *pos, int cap, long long v) {
    int n = 0;
    if (v < 0) {
        n += put_buf(buf, pos, cap, '-');
        v = -v;
    }
    return n + put_uint(buf, pos, cap, (unsigned long long)v, 10, 0);
}

int vprintf_internal(int fd, const char *fmt, va_list ap) {
    char buf[1024];
    int pos = 0;
    int cap = (int)sizeof(buf);
    int total = 0;

    while (*fmt) {
        if (*fmt != '%') {
            total += put_buf(buf, &pos, cap, *fmt++);
            continue;
        }
        fmt++;  /* skip % */

        /* Skip flags. */
        while (*fmt == '-' || *fmt == '+' || *fmt == ' ' || *fmt == '#' || *fmt == '0') fmt++;

        /* Skip width. */
        while (*fmt >= '0' && *fmt <= '9') fmt++;

        /* Skip precision. */
        if (*fmt == '.') {
            fmt++;
            while (*fmt >= '0' && *fmt <= '9') fmt++;
        }

        /* Length modifiers. */
        int is_long = 0, is_longlong = 0, is_short = 0;
        while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z' || *fmt == 'j' || *fmt == 't') {
            if (*fmt == 'l') {
                if (is_long) is_longlong = 1;
                is_long = 1;
            } else if (*fmt == 'h') {
                is_short = 1;
            }
            fmt++;
        }
        /* is_short is parsed for %h / %hh support; short values are
         * currently promoted to int by the variadic ABI so no special
         * handling is needed in the switch below. Kept for future
         * half-word write support. */
        (void)is_short;

        char spec = *fmt++;
        switch (spec) {
            case 'd': case 'i': {
                long long v;
                if (is_longlong) v = va_arg(ap, long long);
                else if (is_long) v = va_arg(ap, long);
                else v = va_arg(ap, int);
                total += put_int(buf, &pos, cap, v);
                break;
            }
            case 'u': {
                unsigned long long v;
                if (is_longlong) v = va_arg(ap, unsigned long long);
                else if (is_long) v = va_arg(ap, unsigned long);
                else v = va_arg(ap, unsigned int);
                total += put_uint(buf, &pos, cap, v, 10, 0);
                break;
            }
            case 'x': case 'X': {
                unsigned long long v;
                if (is_longlong) v = va_arg(ap, unsigned long long);
                else if (is_long) v = va_arg(ap, unsigned long);
                else v = va_arg(ap, unsigned int);
                total += put_uint(buf, &pos, cap, v, 16, spec == 'X');
                break;
            }
            case 'o': {
                unsigned long long v;
                if (is_longlong) v = va_arg(ap, unsigned long long);
                else if (is_long) v = va_arg(ap, unsigned long);
                else v = va_arg(ap, unsigned int);
                total += put_uint(buf, &pos, cap, v, 8, 0);
                break;
            }
            case 'c': {
                char c = (char)va_arg(ap, int);
                total += put_buf(buf, &pos, cap, c);
                break;
            }
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                total += put_str(buf, &pos, cap, s);
                break;
            }
            case 'p': {
                unsigned long long v = (unsigned long long)(size_t)va_arg(ap, void *);
                total += put_str(buf, &pos, cap, "0x");
                total += put_uint(buf, &pos, cap, v, 16, 0);
                break;
            }
            case '%': {
                total += put_buf(buf, &pos, cap, '%');
                break;
            }
            default:
                total += put_buf(buf, &pos, cap, '%');
                total += put_buf(buf, &pos, cap, spec);
                break;
        }
    }

    /* Flush. */
    if (pos > 0) {
        write_str(fd, buf, pos);
    }
    return total;
}

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vprintf_internal(STDOUT_FD, fmt, ap);
    va_end(ap);
    return n;
}

int fprintf(void *fp, const char *fmt, ...) {
    int fd = (fp == (void *)2) ? 2 : 1;
    va_list ap;
    va_start(ap, fmt);
    int n = vprintf_internal(fd, fmt, ap);
    va_end(ap);
    return n;
}

int puts(const char *s) {
    int n = (int)_onyx_write(STDOUT_FD, s, strlen(s));
    _onyx_write(STDOUT_FD, "\n", 1);
    return n + 1;
}

int putchar(int c) {
    char ch = (char)c;
    _onyx_write(STDOUT_FD, &ch, 1);
    return c;
}

int fflush(void *fp) { (void)fp; return 0; }
