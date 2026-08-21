/*
 * string.c — string functions (libonyxc v0.5).
 *
 * Includes the original C99 string functions plus v0.5 additions:
 *   strnlen, strncat, strrchr, strstr, strcasecmp, strncasecmp,
 *   strpbrk, strcspn, strspn, strtok_r, memchr, strcoll, strxfrm.
 */
#include "onyxc.h"

size_t strlen(const char *s) {
    const char *p = s;
    while (*p) p++;
    return (size_t)(p - s);
}

size_t strnlen(const char *s, size_t n) {
    const char *p = s;
    while (n > 0 && *p) { p++; n--; }
    return (size_t)(p - s);
}

char *strcpy(char *d, const char *s) {
    char *r = d;
    while ((*d++ = *s++)) ;
    return r;
}

char *strncpy(char *d, const char *s, size_t n) {
    size_t i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}

int strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = tolower((int)(unsigned char)*a);
        int cb = tolower((int)(unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int strncasecmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (!a[i] || !b[i]) return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        int ca = tolower((int)(unsigned char)a[i]);
        int cb = tolower((int)(unsigned char)b[i]);
        if (ca != cb) return ca - cb;
    }
    return 0;
}

char *strcat(char *d, const char *s) {
    char *r = d;
    while (*d) d++;
    while ((*d++ = *s++)) ;
    return r;
}

char *strncat(char *d, const char *s, size_t n) {
    char *r = d;
    while (*d) d++;
    size_t i = 0;
    while (i < n && s[i]) { *d++ = s[i]; i++; }
    *d = 0;
    return r;
}

char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (c == 0) ? (char *)s : NULL;
}

char *strrchr(const char *s, int c) {
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    if (c == 0) return (char *)s;
    return (char *)last;
}

char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char *)haystack;
    }
    return NULL;
}

char *strpbrk(const char *s, const char *accept) {
    for (; *s; s++) {
        for (const char *a = accept; *a; a++) {
            if (*s == *a) return (char *)s;
        }
    }
    return NULL;
}

size_t strcspn(const char *s, const char *reject) {
    size_t n = 0;
    for (; *s; s++, n++) {
        for (const char *r = reject; *r; r++) {
            if (*s == *r) return n;
        }
    }
    return n;
}

size_t strspn(const char *s, const char *accept) {
    size_t n = 0;
    for (; *s; s++, n++) {
        int found = 0;
        for (const char *a = accept; *a; a++) {
            if (*s == *a) { found = 1; break; }
        }
        if (!found) return n;
    }
    return n;
}

char *strtok_r(char *str, const char *delim, char **saveptr) {
    if (!str) str = *saveptr;
    if (!str || !*str) return NULL;
    /* Skip leading delimiters. */
    str += strspn(str, delim);
    if (!*str) { *saveptr = str; return NULL; }
    char *start = str;
    char *end = start + strcspn(start, delim);
    if (*end) {
        *end = 0;
        *saveptr = end + 1;
    } else {
        *saveptr = end;
    }
    return start;
}

void *memcpy(void *d, const void *s, size_t n) {
    unsigned char *dp = (unsigned char *)d;
    const unsigned char *sp = (const unsigned char *)s;
    for (size_t i = 0; i < n; i++) dp[i] = sp[i];
    return d;
}

void *memset(void *d, int c, size_t n) {
    unsigned char *dp = (unsigned char *)d;
    for (size_t i = 0; i < n; i++) dp[i] = (unsigned char)c;
    return d;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *ap = (const unsigned char *)a;
    const unsigned char *bp = (const unsigned char *)b;
    for (size_t i = 0; i < n; i++) {
        if (ap[i] != bp[i]) return (int)ap[i] - (int)bp[i];
    }
    return 0;
}

void *memmove(void *d, const void *s, size_t n) {
    unsigned char *dp = (unsigned char *)d;
    const unsigned char *sp = (const unsigned char *)s;
    if (dp == sp || n == 0) return d;
    if (dp < sp) {
        for (size_t i = 0; i < n; i++) dp[i] = sp[i];
    } else {
        for (size_t i = n; i > 0; i--) dp[i - 1] = sp[i - 1];
    }
    return d;
}

void *memchr(const void *s, int c, size_t n) {
    const unsigned char *sp = (const unsigned char *)s;
    for (size_t i = 0; i < n; i++) {
        if (sp[i] == (unsigned char)c) return (void *)(sp + i);
    }
    return NULL;
}

char *strdup(const char *s) {
    size_t len = strlen(s);
    char *p = malloc(len + 1);
    if (!p) return NULL;
    memcpy(p, s, len + 1);
    return p;
}

int strcoll(const char *a, const char *b) { return strcmp(a, b); }

size_t strxfrm(char *dst, const char *src, size_t n) {
    size_t len = strlen(src);
    if (dst && n > 0) {
        size_t copy = len < n ? len : n - 1;
        memcpy(dst, src, copy);
        dst[copy] = 0;
    }
    return len;
}
