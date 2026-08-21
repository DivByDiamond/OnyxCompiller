/*
 * stdlib.c — stdlib functions (libonyxc v0.5).
 *
 * Adds: strtoul, strtoll, strtoull, strtod, qsort, bsearch, labs, llabs,
 * itoa, atexit, environ_get.
 *
 * malloc is still a simple bump allocator on top of sbrk(); free is a no-op
 * (memory is reclaimed only on process exit). Suitable for short-lived CLI
 * tools; for long-running processes a real allocator should be added later.
 */
#include "onyxc.h"

extern char **environ;

static void *g_heap_ptr = NULL;
static void *g_heap_end = NULL;

static void heap_init(void) {
    if (!g_heap_ptr) {
        g_heap_ptr = _onyx_sbrk(0);
        g_heap_end = g_heap_ptr;
    }
}

void *malloc(size_t n) {
    heap_init();
    n = (n + 15) & ~15UL;
    if ((char *)g_heap_ptr + n > (char *)g_heap_end) {
        long inc = (long)(n - ((char *)g_heap_end - (char *)g_heap_ptr));
        inc = (inc + 0xFFFF) & ~0xFFFFL;
        void *new_end = _onyx_sbrk(inc);
        if (new_end == (void *)-1 || new_end == NULL) return NULL;
        g_heap_end = (char *)new_end + inc;
    }
    void *p = g_heap_ptr;
    g_heap_ptr = (char *)g_heap_ptr + n;
    return p;
}

void free(void *p) { (void)p; }

void *calloc(size_t n, size_t sz) {
    size_t total = n * sz;
    void *p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void *realloc(void *p, size_t n) {
    void *q = malloc(n);
    if (q && p) {
        memcpy(q, p, n);
    }
    return q;
}

void exit(int code) {
    /* Run atexit handlers in reverse order. */
    extern int atexit_run_handlers(void);
    atexit_run_handlers();
    _onyx_exit(code);
}

int atoi(const char *s) {
    return (int)strtol(s, NULL, 10);
}

long strtol(const char *s, char **endp, int base) {
    long sign = 1;
    long v = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    if (base == 0) {
        if (*s == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (*s == '0') { base = 8; s++; }
        else base = 10;
    }
    while (*s) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * base + d;
        s++;
    }
    if (endp) *endp = (char *)s;
    return v * sign;
}

unsigned long strtoul(const char *s, char **endp, int base) {
    unsigned long v = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '+') s++;
    else if (*s == '-') s++;   /* strtoul on negative is allowed by C99; result is -v as unsigned. */
    if (base == 0) {
        if (*s == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (*s == '0') { base = 8; s++; }
        else base = 10;
    }
    while (*s) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * base + d;
        s++;
    }
    if (endp) *endp = (char *)s;
    return v;
}

long long strtoll(const char *s, char **endp, int base) {
    long long sign = 1;
    long long v = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    if (base == 0) {
        if (*s == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (*s == '0') { base = 8; s++; }
        else base = 10;
    }
    while (*s) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * base + d;
        s++;
    }
    if (endp) *endp = (char *)s;
    return v * sign;
}

unsigned long long strtoull(const char *s, char **endp, int base) {
    unsigned long long v = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '+') s++;
    else if (*s == '-') s++;
    if (base == 0) {
        if (*s == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (*s == '0') { base = 8; s++; }
        else base = 10;
    }
    while (*s) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * base + d;
        s++;
    }
    if (endp) *endp = (char *)s;
    return v;
}

double strtod(const char *s, char **endp) {
    double v = 0.0;
    double sign = 1.0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { sign = -1.0; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') {
        v = v * 10.0 + (double)(*s - '0');
        s++;
    }
    if (*s == '.') {
        s++;
        double scale = 0.1;
        while (*s >= '0' && *s <= '9') {
            v += (double)(*s - '0') * scale;
            scale *= 0.1;
            s++;
        }
    }
    if (*s == 'e' || *s == 'E') {
        s++;
        int esign = 1;
        if (*s == '-') { esign = -1; s++; }
        else if (*s == '+') s++;
        int e = 0;
        while (*s >= '0' && *s <= '9') {
            e = e * 10 + (*s - '0');
            s++;
        }
        double mult = 1.0;
        for (int i = 0; i < e; i++) mult *= 10.0;
        if (esign < 0) v /= mult; else v *= mult;
    }
    if (endp) *endp = (char *)s;
    return v * sign;
}

/* ── Environment ──────────────────────────────────────────────────────── */

char *getenv(const char *name) {
    if (!environ || !name) return NULL;
    size_t name_len = strlen(name);
    for (char **e = environ; *e; e++) {
        char *entry = *e;
        if (strlen(entry) >= name_len &&
            memcmp(entry, name, name_len) == 0 &&
            entry[name_len] == '=') {
            return entry + name_len + 1;
        }
    }
    return NULL;
}

int setenv(const char *name, const char *value, int overwrite) {
    if (!name || !value) return -1;
    if (!overwrite && getenv(name)) return 0;
    size_t name_len = strlen(name);
    size_t value_len = strlen(value);
    char *buf = malloc(name_len + value_len + 2);
    if (!buf) return -1;
    memcpy(buf, name, name_len);
    buf[name_len] = '=';
    memcpy(buf + name_len + 1, value, value_len);
    buf[name_len + 1 + value_len] = 0;
    int count = 0;
    if (environ) {
        while (environ[count]) count++;
    }
    char **new_env = malloc((size_t)(count + 2) * sizeof(char *));
    if (!new_env) return -1;
    for (int i = 0; i < count; i++) new_env[i] = environ[i];
    new_env[count] = buf;
    new_env[count + 1] = NULL;
    environ = new_env;
    return 0;
}

int unsetenv(const char *name) {
    if (!environ || !name) return -1;
    size_t name_len = strlen(name);
    char **dst = environ;
    for (char **src = environ; *src; src++) {
        char *entry = *src;
        if (strlen(entry) >= name_len &&
            memcmp(entry, name, name_len) == 0 &&
            entry[name_len] == '=') {
            continue;
        }
        *dst++ = *src;
    }
    *dst = NULL;
    return 0;
}

void abort(void) {
    _onyx_kill(_onyx_getpid(), 6 /* SIGABRT */);
    _onyx_exit(134);
}

int abs(int n) { return n < 0 ? -n : n; }
long labs(long n) { return n < 0 ? -n : n; }
long long llabs(long long n) { return n < 0 ? -n : n; }

char *itoa(int v, char *buf, int base) {
    if (!buf) return NULL;
    if (base < 2 || base > 36) { buf[0] = 0; return buf; }
    char tmp[32];
    int n = 0;
    int sign = 0;
    unsigned int u;
    if (v < 0 && base == 10) {
        sign = 1;
        u = (unsigned int)(-v);
    } else {
        u = (unsigned int)v;
    }
    if (u == 0) { tmp[n++] = '0'; }
    while (u > 0) {
        int d = (int)(u % (unsigned)base);
        tmp[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        u /= (unsigned)base;
    }
    if (sign) tmp[n++] = '-';
    for (int i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
    buf[n] = 0;
    return buf;
}

/* ── atexit handlers ─────────────────────────────────────────────────── */
#define ATEXIT_MAX 32
static void (*atexit_funcs[ATEXIT_MAX])(void);
static int atexit_n = 0;

int atexit(void (*fn)(void)) {
    if (!fn) return -1;
    if (atexit_n >= ATEXIT_MAX) return -1;
    atexit_funcs[atexit_n++] = fn;
    return 0;
}

int atexit_run_handlers(void) {
    for (int i = atexit_n - 1; i >= 0; i--) {
        if (atexit_funcs[i]) atexit_funcs[i]();
    }
    atexit_n = 0;
    return 0;
}

int atexit_count_get(void) { return atexit_n; }

/* ── qsort / bsearch ─────────────────────────────────────────────────── */

/* Simple iterative quicksort with median-of-three pivot. Falls back to
 * insertion sort for small ranges. */
static void swap_bytes(char *a, char *b, size_t sz) {
    char tmp[64];
    while (sz > 0) {
        size_t chunk = sz < sizeof(tmp) ? sz : sizeof(tmp);
        memcpy(tmp, a, chunk);
        memcpy(a, b, chunk);
        memcpy(b, tmp, chunk);
        a += chunk; b += chunk; sz -= chunk;
    }
}

void qsort(void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *)) {
    if (n < 2 || !base || !cmp) return;
    /* Iterative quicksort with manual stack. */
    typedef struct { char *lo; char *hi; } range_t;
    range_t stack[64];
    int sp = 0;
    stack[sp].lo = (char *)base;
    stack[sp].hi = (char *)base + (n - 1) * sz;
    sp++;
    while (sp > 0) {
        sp--;
        char *lo = stack[sp].lo;
        char *hi = stack[sp].hi;
        while (lo < hi) {
            /* Insertion sort for small ranges. */
            if ((size_t)(hi - lo) / sz < 8) {
                for (char *p = lo + sz; p <= hi; p += sz) {
                    char *q = p;
                    while (q > lo && cmp(q - sz, q) > 0) {
                        swap_bytes(q - sz, q, sz);
                        q -= sz;
                    }
                }
                break;
            }
            /* Median-of-three pivot. */
            char *mid = lo + (size_t)((hi - lo) / sz / 2) * sz;
            if (cmp(lo, mid) > 0) swap_bytes(lo, mid, sz);
            if (cmp(lo, hi) > 0) swap_bytes(lo, hi, sz);
            if (cmp(mid, hi) > 0) swap_bytes(mid, hi, sz);
            char *pivot = mid;
            char *i = lo;
            char *j = hi;
            while (1) {
                while (i < j && cmp(i, pivot) < 0) i += sz;
                while (i < j && cmp(j, pivot) > 0) j -= sz;
                if (i >= j) break;
                swap_bytes(i, j, sz);
                if (i == pivot) pivot = j;
                else if (j == pivot) pivot = i;
                i += sz;
                j -= sz;
            }
            /* Push larger side to stack, recurse on smaller. */
            if (j - lo < hi - j) {
                if (j + sz < hi) {
                    stack[sp].lo = j + sz;
                    stack[sp].hi = hi;
                    sp++;
                }
                hi = j;
            } else {
                if (lo < j - sz) {
                    stack[sp].lo = lo;
                    stack[sp].hi = j - sz;
                    sp++;
                }
                lo = j + sz;
            }
        }
    }
}

void *bsearch(const void *key, const void *base, size_t n, size_t sz,
              int (*cmp)(const void *, const void *)) {
    if (!key || !base || !cmp || n == 0) return NULL;
    const char *p = (const char *)base;
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int c = cmp(key, p + mid * sz);
        if (c == 0) return (void *)(p + mid * sz);
        if (c < 0) hi = mid;
        else lo = mid + 1;
    }
    return NULL;
}

char **environ_get(void) { return environ; }
