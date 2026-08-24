/*
 * pp.c — minimal C preprocessor.
 *
 * Scope: enough for `#include <stdio.h>` from libonyxc + simple
 * `#define NAME value` macros + #ifndef guards. This is not a fully
 * conforming C pp; corner cases (recursive macros, varargs, stringify)
 * are deferred.
 *
 * Implementation: line-oriented, not token-based. Conditional sections
 * are skipped by tracking #if/#else/#endif nesting. Macros are expanded
 * with a single-pass substitution to keep code small.
 */
#include "core/compat.h"
#define _POSIX_C_SOURCE 200809L   /* for strdup */


#include "core/cc.h"
#include "back/pp.h"

#define MAX_INCLUDES 16
#define MAX_MACROS 512
#define MAX_IF_DEPTH 64

typedef struct {
    char name[CC_MAX_IDENT];
    bool is_function_like;
    int nparams;
    char params[CC_MAX_MACRO_ARGS][CC_MAX_IDENT];
    char body[1024];
    bool active;
} macro_t;

static macro_t g_macros[MAX_MACROS];
static int g_n_macros;

static const char *g_inc_paths[16];
static int g_n_inc_paths;

static const char *g_predefines[64];
static int g_n_predefines;

static char g_seen_files[MAX_INCLUDES][CC_MAX_PATH];
static int g_n_seen;

static char *g_out;          /* growing output buffer */
static size_t g_out_len, g_out_cap;

static int g_if_stack[MAX_IF_DEPTH];   /* 1 = currently true, 0 = skipping */
static int g_if_depth;
static int g_if_taken[MAX_IF_DEPTH];   /* 1 if any branch was taken */

static const char *g_cur_file;        /* for #line */
static int g_cur_line;

static void out_emit(const char *s, size_t n) {
    if (g_out_len + n + 1 > g_out_cap) {
        size_t nc = g_out_cap ? g_out_cap : 65536;
        while (nc < g_out_len + n + 1) nc <<= 1;
        char *p = (char *)realloc(g_out, nc);
        if (!p) cc_fatal("pp: out of memory");
        g_out = p;
        g_out_cap = nc;
    }
    memcpy(g_out + g_out_len, s, n);
    g_out_len += n;
}

static void out_emit_str(const char *s) { out_emit(s, strlen(s)); }

static void out_emitf(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) out_emit(buf, n);
}

static bool skipping(void) {
    if (g_if_depth <= 0) return false;
    return g_if_stack[g_if_depth - 1] == 0;
}

static macro_t *find_macro(const char *name) {
    for (int i = 0; i < g_n_macros; i++) {
        if (g_macros[i].active && strcmp(g_macros[i].name, name) == 0) {
            return &g_macros[i];
        }
    }
    return NULL;
}

static void define_macro(const char *spec) {
    /* spec is either "NAME" or "NAME=value" or "NAME(a,b)=body" (rare for CLI). */
    char name[CC_MAX_IDENT];
    const char *eq = strchr(spec, '=');
    const char *body = "";
    size_t nlen;
    if (eq) {
        nlen = eq - spec;
        body = eq + 1;
    } else {
        nlen = strlen(spec);
    }
    if (nlen >= CC_MAX_IDENT) nlen = CC_MAX_IDENT - 1;
    memcpy(name, spec, nlen);
    name[nlen] = 0;

    macro_t *m = find_macro(name);
    if (!m) {
        if (g_n_macros >= MAX_MACROS) cc_fatal("too many macros");
        m = &g_macros[g_n_macros++];
        memset(m, 0, sizeof(*m));
        strncpy(m->name, name, CC_MAX_IDENT - 1);
        m->active = true;
    }
    strncpy(m->body, body, sizeof(m->body) - 1);
    m->is_function_like = false;
}

static bool is_ident_start(int c) { return isalpha(c) || c == '_'; }
static bool is_ident_char(int c)  { return isalnum(c) || c == '_'; }

/* Expand a single identifier at position *p, advancing *p past the
 * expansion. Returns true if expanded. */
static bool expand_one(const char **p, char *out, size_t *outlen, size_t outcap) {
    const char *s = *p;
    char name[CC_MAX_IDENT];
    size_t nl = 0;
    while (is_ident_char(s[nl]) && nl < CC_MAX_IDENT - 1) {
        name[nl] = s[nl];
        nl++;
    }
    name[nl] = 0;
    macro_t *m = find_macro(name);
    if (!m || m->is_function_like) {
        /* No expansion. */
        size_t need = nl;
        if (*outlen + need >= outcap) return false;
        memcpy(out + *outlen, name, need);
        *outlen += need;
        *p = s + nl;
        return true;
    }
    /* Substitute. */
    size_t blen = strlen(m->body);
    if (*outlen + blen >= outcap) return false;
    memcpy(out + *outlen, m->body, blen);
    *outlen += blen;
    *p = s + nl;
    return true;
}

/* Expand one pass over a logical line. Helper for expand_macros. */
static size_t expand_macros_one_pass(const char *in, size_t inlen, char *out, size_t outcap) {
    size_t outlen = 0;
    const char *p = in;
    const char *end = in + inlen;
    while (p < end) {
        int c = (unsigned char)*p;
        if (is_ident_start(c)) {
            if (!expand_one(&p, out, &outlen, outcap)) {
                /* No expansion — copy identifier verbatim. */
                size_t nl = 0;
                while (p + nl < end && is_ident_char(p[nl])) nl++;
                if (outlen + nl < outcap) {
                    memcpy(out + outlen, p, nl);
                    outlen += nl;
                }
                p += nl;
            }
        } else if (c == '"') {
            /* Skip string literal verbatim. */
            if (outlen < outcap) out[outlen++] = *p++;
            else p++;
            while (p < end && *p != '"') {
                if (*p == '\\' && p + 1 < end) {
                    if (outlen < outcap) out[outlen++] = *p++;
                    else p++;
                    if (outlen < outcap) out[outlen++] = *p++;
                    else p++;
                } else if (outlen < outcap) {
                    out[outlen++] = *p++;
                } else { p++; }
            }
            if (p < end && outlen < outcap) out[outlen++] = *p++;
        } else if (c == '\'') {
            if (outlen < outcap) out[outlen++] = *p++;
            else p++;
            while (p < end && *p != '\'') {
                if (*p == '\\' && p + 1 < end) {
                    if (outlen < outcap) out[outlen++] = *p++;
                    else p++;
                    if (outlen < outcap) out[outlen++] = *p++;
                    else p++;
                } else if (outlen < outcap) {
                    out[outlen++] = *p++;
                } else { p++; }
            }
            if (p < end && outlen < outcap) out[outlen++] = *p++;
        } else {
            if (outlen < outcap) out[outlen++] = *p++;
            else p++;
        }
    }
    out[outlen] = 0;
    return outlen;
}

/* Expand macros in a logical line. Result is written back into `line`
 * (in place; assumes expansion does not grow the line significantly).
 * Performs multiple passes to handle chained macro expansions like
 *   #define A B
 *   #define B 1
 * Returns the new length. */
static size_t expand_macros(char *line, size_t len, size_t cap) {
    static char tmp1[8192];
    static char tmp2[8192];
    char *cur = tmp1;
    char *next = tmp2;
    size_t curlen = len;
    memcpy(cur, line, len);
    cur[len] = 0;
    for (int pass = 0; pass < 16; pass++) {
        size_t nextlen = expand_macros_one_pass(cur, curlen, next, sizeof(tmp2) - 1);
        if (nextlen == curlen && memcmp(cur, next, curlen) == 0) {
            /* No change — stable. */
            break;
        }
        char *swap = cur; cur = next; next = swap;
        curlen = nextlen;
        if (curlen >= sizeof(tmp1) - 1) break;
    }
    if (curlen >= cap) curlen = cap - 1;
    memcpy(line, cur, curlen);
    line[curlen] = 0;
    return curlen;
}

/* Find include file. Returns malloc'd full path or NULL. */
static char *find_include(const char *name, bool is_system) {
    /* Try system paths first if system include, then local. */
    static char buf[CC_MAX_PATH * 2];
    if (!is_system) {
        /* Local: relative to current file's directory. */
        if (g_cur_file) {
            const char *slash = strrchr(g_cur_file, '/');
            if (slash) {
                size_t dn = slash - g_cur_file + 1;
                if (dn + strlen(name) < sizeof(buf)) {
                    memcpy(buf, g_cur_file, dn);
                    strcpy(buf + dn, name);
                    FILE *f = fopen(buf, "rb");
                    if (f) { fclose(f); return strdup(buf); }
                }
            }
        }
        /* Fall through to system paths. */
    }
    for (int i = 0; i < g_n_inc_paths; i++) {
        int n = snprintf(buf, sizeof(buf), "%s/%s", g_inc_paths[i], name);
        if (n <= 0 || n >= (int)sizeof(buf)) continue;
        FILE *f = fopen(buf, "rb");
        if (f) { fclose(f); return strdup(buf); }
    }
    return NULL;
}

static bool is_pragma_once(const char *line) {
    /* Match leading "#pragma once". */
    while (*line == ' ' || *line == '\t') line++;
    if (*line != '#') return false;
    line++;
    while (*line == ' ' || *line == '\t') line++;
    return strncmp(line, "pragma", 6) == 0 && (line[6] == ' ' || line[6] == '\t')
        && strstr(line + 7, "once") != NULL;
}

static bool file_seen(const char *path) {
    for (int i = 0; i < g_n_seen; i++) {
        if (strcmp(g_seen_files[i], path) == 0) return true;
    }
    return false;
}

static void remember_file(const char *path) {
    if (g_n_seen >= MAX_INCLUDES) return;
    strncpy(g_seen_files[g_n_seen++], path, CC_MAX_PATH - 1);
}

/* Process one source buffer, emitting to g_out. Recursive for #include.
 * Records include depth and pragma-once state. */
static void process_source(const char *src, size_t len, const char *filename);

/* Evaluate a #if constant expression (very simplified). Supports
 * integer literals, defined(NAME), ! && || == != < > <= >= + - * / %
 * and parens. */
static long eval_const_expr(const char *expr);

static void handle_directive(const char *line, size_t llen, const char *filename, int lineno) {
    /* line points to char after '#'. Skip ws. */
    while (llen > 0 && (*line == ' ' || *line == '\t')) { line++; llen--; }

    /* Extract directive name. */
    char d[16];
    size_t dn = 0;
    while (dn < sizeof(d) - 1 && dn < llen && is_ident_char(line[dn])) {
        d[dn] = line[dn];
        dn++;
    }
    d[dn] = 0;
    const char *rest = line + dn;
    size_t restlen = llen - dn;
    while (restlen > 0 && (*rest == ' ' || *rest == '\t')) { rest++; restlen--; }

    if (strcmp(d, "include") == 0) {
        if (skipping()) return;
        /* rest = "name" or <name>. */
        if (restlen < 2) return;
        bool is_system = (rest[0] == '<');
        char close = is_system ? '>' : '"';
        if (rest[0] != '"' && rest[0] != '<') return;
        const char *p = rest + 1;
        const char *endp = memchr(p, close, restlen - 1);
        if (!endp) return;
        size_t nl = endp - p;
        char name[CC_MAX_PATH];
        if (nl >= sizeof(name)) return;
        memcpy(name, p, nl);
        name[nl] = 0;
        char *full = find_include(name, is_system);
        if (!full) {
            cc_error_at(filename, lineno, "include not found: %s", name);
            return;
        }
        if (file_seen(full)) { free(full); return; }
        size_t flen;
        char *fsrc = pp_read_file(full, &flen);
        if (!fsrc) { free(full); return; }
        /* Note: we intentionally don't emit # line markers — lexer
         * doesn't handle them, and we want errors to point at the
         * real file/line. */
        const char *saved = g_cur_file;
        int saved_line = g_cur_line;
        g_cur_file = full;
        g_cur_line = 1;
        process_source(fsrc, flen, full);
        free(fsrc);
        free(full);
        g_cur_file = saved;
        g_cur_line = saved_line;
        return;
    }

    if (strcmp(d, "define") == 0) {
        if (skipping()) return;
        /* NAME [(params)] body */
        char name[CC_MAX_IDENT];
        size_t nl = 0;
        while (nl < sizeof(name) - 1 && nl < restlen && is_ident_char(rest[nl])) {
            name[nl] = rest[nl]; nl++;
        }
        name[nl] = 0;
        if (nl == 0) return;
        const char *body = rest + nl;
        size_t blen = restlen - nl;
        while (blen > 0 && (*body == ' ' || *body == '\t')) { body++; blen--; }
        bool is_fn = false;
        int np = 0;
        char ps[CC_MAX_MACRO_ARGS][CC_MAX_IDENT];
        /* Function-like if '(' immediately follows NAME (no space). */
        if (nl < restlen && rest[nl] == '(') {
            is_fn = true;
            const char *q = rest + nl + 1;
            const char *qend = rest + restlen;
            while (q < qend && *q != ')') {
                while (q < qend && (*q == ' ' || *q == ',' || *q == '\t')) q++;
                if (*q == ')') break;
                size_t pn = 0;
                while (q < qend && is_ident_char(*q) && pn < CC_MAX_IDENT - 1) {
                    ps[np][pn++] = *q++;
                }
                ps[np][pn] = 0;
                if (pn > 0) np++;
                while (q < qend && *q != ',' && *q != ')') q++;
            }
            /* body starts after ')'. */
            if (q < qend && *q == ')') q++;
            while (q < qend && (*q == ' ' || *q == '\t')) q++;
            body = q;
            blen = qend - q;
        }
        macro_t *m = find_macro(name);
        if (!m) {
            if (g_n_macros >= MAX_MACROS) cc_fatal("too many macros");
            m = &g_macros[g_n_macros++];
        }
        memset(m, 0, sizeof(*m));
        strncpy(m->name, name, CC_MAX_IDENT - 1);
        m->is_function_like = is_fn;
        m->nparams = np;
        for (int i = 0; i < np; i++) strncpy(m->params[i], ps[i], CC_MAX_IDENT - 1);
        if (blen >= sizeof(m->body)) blen = sizeof(m->body) - 1;
        memcpy(m->body, body, blen);
        m->body[blen] = 0;
        m->active = true;
        return;
    }

    if (strcmp(d, "undef") == 0) {
        if (skipping()) return;
        char name[CC_MAX_IDENT];
        size_t nl = 0;
        while (nl < sizeof(name) - 1 && nl < restlen && is_ident_char(rest[nl])) {
            name[nl] = rest[nl]; nl++;
        }
        name[nl] = 0;
        macro_t *m = find_macro(name);
        if (m) m->active = false;
        return;
    }

    if (strcmp(d, "ifdef") == 0) {
        char name[CC_MAX_IDENT];
        size_t nl = 0;
        while (nl < sizeof(name) - 1 && nl < restlen && is_ident_char(rest[nl])) {
            name[nl] = rest[nl]; nl++;
        }
        name[nl] = 0;
        bool taken = (find_macro(name) != NULL);
        if (g_if_depth >= MAX_IF_DEPTH) cc_fatal("#if nesting too deep");
        g_if_stack[g_if_depth] = skipping() ? 0 : (taken ? 1 : 0);
        g_if_taken[g_if_depth] = taken;
        g_if_depth++;
        return;
    }

    if (strcmp(d, "ifndef") == 0) {
        char name[CC_MAX_IDENT];
        size_t nl = 0;
        while (nl < sizeof(name) - 1 && nl < restlen && is_ident_char(rest[nl])) {
            name[nl] = rest[nl]; nl++;
        }
        name[nl] = 0;
        bool taken = (find_macro(name) == NULL);
        if (g_if_depth >= MAX_IF_DEPTH) cc_fatal("#if nesting too deep");
        g_if_stack[g_if_depth] = skipping() ? 0 : (taken ? 1 : 0);
        g_if_taken[g_if_depth] = taken;
        g_if_depth++;
        return;
    }

    if (strcmp(d, "if") == 0) {
        char exprbuf[1024];
        if (restlen >= sizeof(exprbuf)) restlen = sizeof(exprbuf) - 1;
        memcpy(exprbuf, rest, restlen);
        exprbuf[restlen] = 0;
        size_t el = expand_macros(exprbuf, restlen, sizeof(exprbuf));
        long v = eval_const_expr(exprbuf);
        (void)el;
        bool taken = (v != 0);
        if (g_if_depth >= MAX_IF_DEPTH) cc_fatal("#if nesting too deep");
        g_if_stack[g_if_depth] = skipping() ? 0 : (taken ? 1 : 0);
        g_if_taken[g_if_depth] = taken;
        g_if_depth++;
        return;
    }

    if (strcmp(d, "elif") == 0) {
        if (g_if_depth <= 0) return;
        if (g_if_taken[g_if_depth - 1]) {
            /* Already taken a branch. */
            g_if_stack[g_if_depth - 1] = 0;
        } else {
            char exprbuf[1024];
            if (restlen >= sizeof(exprbuf)) restlen = sizeof(exprbuf) - 1;
            memcpy(exprbuf, rest, restlen);
            exprbuf[restlen] = 0;
            expand_macros(exprbuf, restlen, sizeof(exprbuf));
            long v = eval_const_expr(exprbuf);
            if (v != 0) {
                g_if_stack[g_if_depth - 1] = 1;
                g_if_taken[g_if_depth - 1] = true;
            } else {
                g_if_stack[g_if_depth - 1] = 0;
            }
        }
        return;
    }

    if (strcmp(d, "else") == 0) {
        if (g_if_depth <= 0) return;
        if (g_if_taken[g_if_depth - 1]) {
            g_if_stack[g_if_depth - 1] = 0;
        } else {
            g_if_stack[g_if_depth - 1] = 1;
            g_if_taken[g_if_depth - 1] = true;
        }
        return;
    }

    if (strcmp(d, "endif") == 0) {
        if (g_if_depth > 0) g_if_depth--;
        return;
    }

    if (strcmp(d, "pragma") == 0) {
        if (skipping()) return;
        if (is_pragma_once(line - 1)) {
            if (g_cur_file) remember_file(g_cur_file);
        }
        /* Otherwise ignore. */
        return;
    }

    if (strcmp(d, "line") == 0) {
        /* ignore */
        return;
    }

    if (strcmp(d, "error") == 0) {
        if (!skipping()) {
            char msg[256];
            size_t ml = restlen < sizeof(msg) - 1 ? restlen : sizeof(msg) - 1;
            memcpy(msg, rest, ml);
            msg[ml] = 0;
            cc_error_at(filename, lineno, "#error: %s", msg);
        }
        return;
    }
    /* Unknown directive: silently ignore (could warn). */
}

/* Recursive-descent mini-evaluator for #if expressions — file scope. */
static const char *g_ifs;

static long if_or(void);
static long if_and(void);
static long if_eq(void);
static long if_rel(void);
static long if_add(void);
static long if_mul(void);
static long if_unary(void);
static long if_primary(void);

#define IF_SKIP_WS() while (*g_ifs == ' ' || *g_ifs == '\t' || *g_ifs == '\n') g_ifs++

static long if_primary(void) {
    IF_SKIP_WS();
    if (*g_ifs == '(') {
        g_ifs++;
        long v = if_or();
        IF_SKIP_WS();
        if (*g_ifs == ')') g_ifs++;
        return v;
    }
    if (isdigit((unsigned char)*g_ifs)) {
        long v = 0;
        if (g_ifs[0] == '0' && (g_ifs[1] == 'x' || g_ifs[1] == 'X')) {
            g_ifs += 2;
            while (isxdigit((unsigned char)*g_ifs)) {
                int d = isdigit(*g_ifs) ? *g_ifs - '0' : (tolower(*g_ifs) - 'a' + 10);
                v = v * 16 + d;
                g_ifs++;
            }
        } else {
            while (isdigit((unsigned char)*g_ifs)) {
                v = v * 10 + (*g_ifs - '0');
                g_ifs++;
            }
        }
        while (*g_ifs == 'u' || *g_ifs == 'U' || *g_ifs == 'l' || *g_ifs == 'L') g_ifs++;
        return v;
    }
    if (is_ident_start(*g_ifs)) {
        while (is_ident_char(*g_ifs)) g_ifs++;
        return 0;
    }
    return 0;
}

static long if_unary(void) {
    IF_SKIP_WS();
    if (*g_ifs == '!') { g_ifs++; return !if_unary(); }
    if (*g_ifs == '-') { g_ifs++; return -if_unary(); }
    if (*g_ifs == '+') { g_ifs++; return if_unary(); }
    if (*g_ifs == '~') { g_ifs++; return ~if_unary(); }
    return if_primary();
}

static long if_mul(void) {
    long a = if_unary();
    IF_SKIP_WS();
    while (*g_ifs == '*' || *g_ifs == '/' || *g_ifs == '%') {
        char op = *g_ifs++;
        long b = if_unary();
        if (op == '*') a = a * b;
        else if (op == '/') a = b ? a / b : 0;
        else a = b ? a % b : 0;
        IF_SKIP_WS();
    }
    return a;
}

static long if_add(void) {
    long a = if_mul();
    IF_SKIP_WS();
    while (*g_ifs == '+' || *g_ifs == '-') {
        char op = *g_ifs++;
        long b = if_mul();
        a = op == '+' ? a + b : a - b;
        IF_SKIP_WS();
    }
    return a;
}

static long if_rel(void) {
    long a = if_add();
    IF_SKIP_WS();
    while ((*g_ifs == '<' && g_ifs[1] != '<') || (*g_ifs == '>' && g_ifs[1] != '>') ||
           (*g_ifs == '<' && g_ifs[1] == '=') || (*g_ifs == '>' && g_ifs[1] == '=')) {
        char op = *g_ifs;
        int eq = (g_ifs[1] == '=');
        g_ifs += 1 + eq;
        long b = if_add();
        if (op == '<') a = eq ? (a <= b) : (a < b);
        else           a = eq ? (a >= b) : (a > b);
        IF_SKIP_WS();
    }
    return a;
}

static long if_eq(void) {
    long a = if_rel();
    IF_SKIP_WS();
    while ((g_ifs[0] == '=' && g_ifs[1] == '=') || (g_ifs[0] == '!' && g_ifs[1] == '=')) {
        int ne = (g_ifs[0] == '!');
        g_ifs += 2;
        long b = if_rel();
        a = ne ? (a != b) : (a == b);
        IF_SKIP_WS();
    }
    return a;
}

static long if_and(void) {
    long a = if_eq();
    IF_SKIP_WS();
    while (*g_ifs == '&' && g_ifs[1] != '&') {
        g_ifs++;
        long b = if_eq();
        a = a & b;
        IF_SKIP_WS();
    }
    return a;
}

static long if_or(void) {
    long a = if_and();
    IF_SKIP_WS();
    while (*g_ifs == '|' && g_ifs[1] != '|') {
        g_ifs++;
        long b = if_and();
        a = a | b;
        IF_SKIP_WS();
    }
    return a;
}

#undef IF_SKIP_WS

static long eval_const_expr(const char *expr) {
    /* First, replace defined(NAME) with 1 or 0. */
    static char buf[2048];
    size_t bl = 0;
    const char *p = expr;
    while (*p && bl < sizeof(buf) - 1) {
        if (strncmp(p, "defined", 7) == 0) {
            p += 7;
            while (*p == ' ' || *p == '\t') p++;
            int paren = 0;
            if (*p == '(') { paren = 1; p++; }
            char name[CC_MAX_IDENT];
            size_t nl = 0;
            while (is_ident_char(*p) && nl < CC_MAX_IDENT - 1) name[nl++] = *p++;
            name[nl] = 0;
            if (paren && *p == ')') p++;
            long v = find_macro(name) ? 1 : 0;
            bl += snprintf(buf + bl, sizeof(buf) - bl, "%ld", v);
        } else {
            buf[bl++] = *p++;
        }
    }
    buf[bl] = 0;

    g_ifs = buf;
    while (*g_ifs == ' ' || *g_ifs == '\t' || *g_ifs == '\n') g_ifs++;
    long a = if_or();
    while (*g_ifs == ' ' || *g_ifs == '\t' || *g_ifs == '\n') g_ifs++;
    while (*g_ifs == '&' && g_ifs[1] == '&') {
        g_ifs += 2;
        long b = if_or();
        a = a && b;
        while (*g_ifs == ' ' || *g_ifs == '\t' || *g_ifs == '\n') g_ifs++;
    }
    while (*g_ifs == '|' && g_ifs[1] == '|') {
        g_ifs += 2;
        long b = if_or();
        a = a || b;
        while (*g_ifs == ' ' || *g_ifs == '\t' || *g_ifs == '\n') g_ifs++;
    }
    return a;
}

static void process_source(const char *src, size_t len, const char *filename) {
    const char *p = src;
    const char *end = src + len;
    int lineno = 1;
    while (p < end) {
        /* Find end of line. */
        const char *lend = memchr(p, '\n', end - p);
        if (!lend) lend = end;
        size_t llen = lend - p;

        /* Strip trailing \r. */
        while (llen > 0 && (p[llen - 1] == '\r' || p[llen - 1] == '\n')) llen--;

        /* Directive? */
        const char *q = p;
        size_t qlen = llen;
        while (qlen > 0 && (*q == ' ' || *q == '\t')) { q++; qlen--; }
        if (qlen > 0 && q[0] == '#') {
            handle_directive(q + 1, qlen - 1, filename, lineno);
        } else if (!skipping()) {
            /* Expand macros in non-directive lines. */
            char linebuf[8192];
            if (qlen < sizeof(linebuf)) {
                memcpy(linebuf, q, qlen);
                linebuf[qlen] = 0;
                size_t nl = expand_macros(linebuf, qlen, sizeof(linebuf));
                out_emit(linebuf, nl);
            } else {
                out_emit(q, qlen);
            }
            out_emit("\n", 1);
        } else {
            /* Emit empty line to preserve line numbers. */
            out_emit("\n", 1);
        }
        p = (lend < end) ? lend + 1 : end;
        lineno++;
    }
}

char *pp_read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, sz, f);
    fclose(f);
    buf[rd] = 0;
    if (out_len) *out_len = rd;
    return buf;
}

char *pp_preprocess_file(const char *path,
                         const char *const *include_paths, int n_include_paths,
                         const char *const *define_macros, int n_define_macros,
                         size_t *out_len) {
    /* Reset state. */
    g_n_macros = 0;
    g_n_inc_paths = 0;
    g_n_predefines = 0;
    g_n_seen = 0;
    g_if_depth = 0;
    g_out = NULL; g_out_len = 0; g_out_cap = 0;
    g_cur_file = path;
    g_cur_line = 1;

    /* Predefined macros. */
    static const char *std_predefines[] = {
        "__onyx__=1",
        "__onyxos__=1",
        "__riscv=1",
        "__riscv_xlen=64",
        "__riscv64__=1",
        "__LP64__=1",
        "__SIZEOF_POINTER__=8",
        "__SIZEOF_LONG__=8",
        "__SIZEOF_LONG_LONG__=8",
        "__SIZEOF_INT__=4",
        "__SIZEOF_SHORT__=2",
        "__SIZEOF_CHAR__=1",
        "__SIZEOF_SIZE_T__=8",
        "__STDC__=1",
        "__STDC_VERSION__=199901L",
        "__STDC_HOSTED__=0",  /* we're freestanding-ish, libonyxc provides a subset */
        NULL,
    };
    for (int i = 0; std_predefines[i]; i++) {
        define_macro(std_predefines[i]);
    }
    for (int i = 0; i < n_define_macros; i++) {
        define_macro(define_macros[i]);
    }
    for (int i = 0; i < n_include_paths; i++) {
        g_inc_paths[i] = include_paths[i];
    }
    g_n_inc_paths = n_include_paths;

    size_t srclen;
    char *src = pp_read_file(path, &srclen);
    if (!src) {
        cc_fatal("cannot read input: %s", path);
    }
    process_source(src, srclen, path);
    free(src);

    if (out_len) *out_len = g_out_len;
    /* Null-terminate for lexer. */
    out_emit("", 1);
    return g_out;
}
