/*
 * pp.c — minimal C preprocessor.
 *
 * Supports object/function macros, includes and conditional expressions.
 * This remains a bounded, line-oriented preprocessor, rather than a fully
 * conforming token-based implementation. Function invocations gather
 * physical lines before repeated expansion; directives stay separate.
 */
#include "core/compat.h"
#define _POSIX_C_SOURCE 200809L   /* for strdup */


#include "core/cc.h"
#include "back/pp.h"

#define MAX_INCLUDES 16
#define MAX_MACROS 4096
#define MAX_IF_DEPTH 64

typedef struct {
    char name[CC_MAX_IDENT];
    bool is_function_like;
    bool is_varargs;          /* last param is ... → __VA_ARGS__ */
    int nparams;
    char params[CC_MAX_MACRO_ARGS][CC_MAX_IDENT];
    char body[1024];
    bool active;
} macro_t;

/* End of the source buffer being macro-expanded (read_macro_args needs
 * it to bound its scan). */
static const char *g_src_end;
static int g_expansion_depth;

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
static int g_if_parent[MAX_IF_DEPTH];
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

/* ── Function-like macro machinery ─────────────────────────────────── */

/* Read the argument list of a function-like macro invocation starting at
 * '(' (*p points AT the paren). Splits top-level commas, respects nested
 * parens/brackets/braces and string literals. Returns the number of args
 * collected (0 for empty ()), -1 on syntax error. Advances *p past ')'. */
static int read_macro_args(const char **p, const char *end,
                           char args[][512], int maxargs) {
    const char *s = *p;
    if (s >= end || *s != '(') return -1;
    s++;   /* consume '(' */
    int nargs = 0;
    int depth = 1;
    bool in_str = false, in_chr = false;
    bool any = false;
    size_t alen = 0;
    while (s < end) {
        char c = *s;
        if (in_str) {
            any = true;
            if (c == '\\' && s + 1 < end) {
                if (nargs < maxargs && alen < 510) { args[nargs][alen++] = c; args[nargs][alen++] = s[1]; }
                s += 2;
                continue;
            }
            if (c == '"') in_str = false;
            if (nargs < maxargs && alen < 511) args[nargs][alen++] = c;
            s++;
            continue;
        }
        if (in_chr) {
            any = true;
            if (c == '\\' && s + 1 < end) {
                if (nargs < maxargs && alen < 510) { args[nargs][alen++] = c; args[nargs][alen++] = s[1]; }
                s += 2;
                continue;
            }
            if (c == '\'') in_chr = false;
            if (nargs < maxargs && alen < 511) args[nargs][alen++] = c;
            s++;
            continue;
        }
        if (c == '"') { in_str = true; any = true; if (nargs < maxargs && alen < 511) args[nargs][alen++] = c; s++; continue; }
        if (c == '\'') { in_chr = true; any = true; if (nargs < maxargs && alen < 511) args[nargs][alen++] = c; s++; continue; }
        if (c == '(' || c == '[' || c == '{') { depth++; any = true; if (nargs < maxargs && alen < 511) args[nargs][alen++] = c; s++; continue; }
        if (c == ')' || c == ']' || c == '}') {
            depth--;
            if (depth == 0) {
                /* End of the invocation. */
                if (any || nargs > 0) {
                    if (nargs < maxargs) {
                        while (alen > 0 && (args[nargs][alen-1] == ' ' || args[nargs][alen-1] == '\t')) alen--;
                        args[nargs][alen] = 0;
                        nargs++;
                    }
                }
                s++;
                *p = s;
                return nargs;
            }
            any = true;
            if (nargs < maxargs && alen < 511) args[nargs][alen++] = c;
            s++;
            continue;
        }
        if (c == ',' && depth == 1) {
            /* Top-level comma: next argument. */
            if (nargs < maxargs) {
                while (alen > 0 && (args[nargs][alen-1] == ' ' || args[nargs][alen-1] == '\t')) alen--;
                args[nargs][alen] = 0;
            }
            nargs++;
            alen = 0;
            any = false;
            s++;
            continue;
        }
        if (isspace((unsigned char)c)) c = ' ';
        else any = true;
        if (c != ' ' || (alen > 0 && nargs < maxargs && args[nargs][alen - 1] != ' ')) {
            if (nargs < maxargs && alen < 511) args[nargs][alen++] = c;
        }
        s++;
    }
    return -1;   /* unterminated */
}

static size_t expand_macros(char *line, size_t len, size_t cap);

/* Prescan only ordinary parameter occurrences. # and ## use the spelling
 * supplied by the caller, not its expanded tokens. Buffers belong to this
 * invocation so recursive argument expansion cannot overwrite outer args. */
static const char *prescan_argument(char args[][512], int index,
                                   char expanded[][512], bool ready[]) {
    if (!ready[index]) {
        size_t length = strlen(args[index]);
        memcpy(expanded[index], args[index], length + 1);
        expand_macros(expanded[index], length, 512);
        ready[index] = true;
    }
    return expanded[index];
}

/* Substitute parameters in a macro body with the given arguments,
 * handling:
 *   #param  → "arg"  (stringize)
 *   a##b    → token paste (no space between)
 *   __VA_ARGS__ → all extra args joined with ", "
 * The result is appended to out. */
static void subst_macro_body(const macro_t *m,
                             char args[][512], int nargs,
                             char *out, size_t outcap, size_t *outlen) {
    const char *b = m->body;
    size_t blen = strlen(b);
    size_t i = 0;
    (void)blen;
    char expanded[CC_MAX_MACRO_ARGS + 8][512];
    bool ready[CC_MAX_MACRO_ARGS + 8] = {0};
    char va_expanded[2048] = {0};
    bool va_ready = false;

    /* Pre-join variadic tail into a single pseudo-arg slot. */
    char va_all[2048] = {0};
    bool has_va = false;
    if (m->is_varargs) {
        has_va = true;
        size_t vl = 0;
        for (int a = m->nparams; a < nargs; a++) {
            if (vl > 0 && vl + 2 < sizeof(va_all)) { va_all[vl++] = ','; va_all[vl++] = ' '; }
            size_t al = strlen(args[a]);
            if (vl + al >= sizeof(va_all)) al = sizeof(va_all) - 1 - vl;
            memcpy(va_all + vl, args[a], al);
            vl += al;
        }
        va_all[vl] = 0;
    }

    while (i < strlen(b)) {
        char c = b[i];
        /* #param — stringize. */
        if (c == '#' && i + 1 < strlen(b) && (is_ident_start(b[i+1]) || b[i+1] == '_') &&
            !(i + 2 < strlen(b) && b[i+1] == '#' && b[i+2] == '#')) {
            /* ( ## handled below; single # is stringize) */
            size_t nl = 0;
            char name[CC_MAX_IDENT];
            i++;
            while (i < strlen(b) && is_ident_char(b[i]) && nl < CC_MAX_IDENT - 1) {
                name[nl++] = b[i++];
            }
            name[nl] = 0;
            /* __VA_ARGS__ stringize */
            const char *val = NULL;
            if (strcmp(name, "__VA_ARGS__") == 0 && has_va) {
                val = va_all;
            } else {
                for (int pi = 0; pi < m->nparams; pi++) {
                    if (strcmp(m->params[pi], name) == 0) {
                        val = (pi < nargs) ? args[pi] : "";
                        break;
                    }
                }
            }
            if (val) {
                if (*outlen + 2 >= outcap) return;
                out[(*outlen)++] = '"';
                for (const char *q = val; *q; q++) {
                    if (*q == '"' || *q == '\\') {
                        if (*outlen + 3 >= outcap) return;
                        out[(*outlen)++] = '\\';
                    }
                    if (*outlen + 2 >= outcap) return;
                    out[(*outlen)++] = *q;
                }
                out[(*outlen)++] = '"';
            } else {
                /* Not a parameter — emit as-is. */
                if (*outlen + 1 + nl < outcap) { out[(*outlen)++] = '#'; memcpy(out + *outlen, name, nl); *outlen += nl; }
            }
            continue;
        }
        /* ## — token paste. Two forms:
         *   a##b          → paste tokens (emit nothing, they become adjacent)
         *   , ##__VA_ARGS__ (GNU) → drop the comma when no variadic args
         */
        if (c == '#' && i + 1 < strlen(b) && b[i+1] == '#') {
            /* Look back: did we just emit a trailing ','? */
            size_t j = i + 2;
            while (j < strlen(b) && (b[j] == ' ' || b[j] == '\t')) j++;
            /* Is the next token __VA_ARGS__? */
            if (has_va && strncmp(b + j, "__VA_ARGS__", 11) == 0) {
                /* GNU comma paste: ", ##__VA_ARGS__" — drop the comma
                 * (and any space we already emitted after it) when there
                 * are no variadic args. out may end with ',', ' ,' or
                 * ',"..."'-style content; scan back over whitespace. */
                size_t k = *outlen;
                while (k > 0 && (out[k-1] == ' ' || out[k-1] == '\t')) k--;
                if (k > 0 && out[k-1] == ',') {
                    if (va_all[0] == 0) {
                        *outlen = k - 1;   /* drop comma + trailing spaces */
                    }
                }
                i = j + 11;
                if (va_all[0] != 0) {
                    size_t vl = strlen(va_all);
                    if (*outlen + vl >= outcap) vl = outcap - 1 - *outlen;
                    memcpy(out + *outlen, va_all, vl);
                    *outlen += vl;
                }
                continue;
            }
            while (*outlen && isspace((unsigned char)out[*outlen - 1])) (*outlen)--;
            i += 2;
            /* skip whitespace after ## */
            while (i < strlen(b) && (b[i] == ' ' || b[i] == '\t')) i++;
            continue;
        }
        /* Identifier: parameters adjacent to ## retain raw spelling. */
        if (is_ident_start(c)) {
            size_t before = i;
            while (before && isspace((unsigned char)b[before - 1])) before--;
            bool pasted = before >= 2 && b[before - 1] == '#' && b[before - 2] == '#';
            size_t nl = 0;
            char name[CC_MAX_IDENT];
            while (i < strlen(b) && is_ident_char(b[i]) && nl < CC_MAX_IDENT - 1) {
                name[nl++] = b[i++];
            }
            name[nl] = 0;
            size_t after = i;
            while (isspace((unsigned char)b[after])) after++;
            if (b[after] == '#' && b[after + 1] == '#') pasted = true;
            const char *val = NULL;
            if (has_va && strcmp(name, "__VA_ARGS__") == 0) {
                if (!pasted && !va_ready) {
                    size_t length = 0;
                    for (int a = m->nparams; a < nargs; a++) {
                        const char *argument = prescan_argument(args, a, expanded, ready);
                        if (a > m->nparams && length + 2 < sizeof(va_expanded)) {
                            va_expanded[length++] = ',';
                            va_expanded[length++] = ' ';
                        }
                        size_t count = strlen(argument);
                        if (length + count >= sizeof(va_expanded)) count = sizeof(va_expanded) - 1 - length;
                        memcpy(va_expanded + length, argument, count);
                        length += count;
                    }
                    va_expanded[length] = 0;
                    va_ready = true;
                }
                val = pasted ? va_all : va_expanded;
            } else {
                for (int pi = 0; pi < m->nparams; pi++) {
                    if (strcmp(m->params[pi], name) == 0) {
                        val = pi >= nargs ? "" : pasted ? args[pi] : prescan_argument(args, pi, expanded, ready);
                        break;
                    }
                }
            }
            if (val) {
                size_t vl = strlen(val);
                if (*outlen + vl >= outcap) vl = outcap - 1 - *outlen;
                memcpy(out + *outlen, val, vl);
                *outlen += vl;
            } else {
                if (*outlen + nl < outcap) {
                    memcpy(out + *outlen, name, nl);
                    *outlen += nl;
                }
            }
            continue;
        }
        /* Regular char. */
        if (*outlen + 1 < outcap) {
            out[(*outlen)++] = c;
        }
        i++;
    }
    out[*outlen] = 0;
}

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
    if (!m) {
        /* No expansion. */
        size_t need = nl;
        if (*outlen + need >= outcap) return false;
        memcpy(out + *outlen, name, need);
        *outlen += need;
        *p = s + nl;
        return true;
    }
    if (!m->is_function_like) {
        /* Object-like: substitute body. */
        size_t blen = strlen(m->body);
        if (*outlen + blen >= outcap) return false;
        memcpy(out + *outlen, m->body, blen);
        *outlen += blen;
        *p = s + nl;
        return true;
    }
    /* Invocation whitespace includes physical newlines. */
    const char *q = s + nl;
    while (q < g_src_end && isspace((unsigned char)*q)) q++;
    if (q >= g_src_end || *q != '(') {
        /* Not an invocation — copy the identifier verbatim. */
        size_t need = nl;
        if (*outlen + need >= outcap) return false;
        memcpy(out + *outlen, name, need);
        *outlen += need;
        *p = s + nl;
        return true;
    }
    /* Read the arguments. */
    char args[CC_MAX_MACRO_ARGS + 8][512];
    int nargs = read_macro_args(&q, g_src_end, args, CC_MAX_MACRO_ARGS + 8);
    if (nargs < 0) {
        /* Unterminated — copy identifier, let the parser complain. */
        size_t need = nl;
        if (*outlen + need >= outcap) return false;
        memcpy(out + *outlen, name, need);
        *outlen += need;
        *p = s + nl;
        return true;
    }
    if (nargs > CC_MAX_MACRO_ARGS + 8) {
        cc_error("pp: too many macro arguments");
        nargs = CC_MAX_MACRO_ARGS + 8;
    }
    /* Variadic macros accept any nargs >= nparams; fixed macros: pad. */
    if (!m->is_varargs && nargs < m->nparams) {
        for (int i = nargs; i < m->nparams; i++) args[i][0] = 0;
        nargs = m->nparams;
    }
    subst_macro_body(m, args, nargs, out, outcap, outlen);
    /* Argument whitespace may be duplicated or discarded by substitution;
     * retain consumed source newlines exactly once after the replacement. */
    for (const char *r = s; r < q; r++) {
        if (*r == '\n' && *outlen + 1 < outcap) out[(*outlen)++] = '\n';
    }
    out[*outlen] = 0;
    *p = q;
    return true;
}

/* Expand one pass over a logical line. Helper for expand_macros. */
static size_t expand_macros_one_pass(const char *in, size_t inlen, char *out, size_t outcap) {
    size_t outlen = 0;
    const char *p = in;
    const char *end = in + inlen;
    g_src_end = end;
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
    if (g_expansion_depth >= 32) {
        cc_error("pp: macro argument nesting too deep");
        return len;
    }
    g_expansion_depth++;
    char tmp1[8192];
    char tmp2[8192];
    const char *saved_end = g_src_end;
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
    g_src_end = saved_end;
    g_expansion_depth--;
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
        bool is_varargs_m = false;
        int np = 0;
        char ps[CC_MAX_MACRO_ARGS][CC_MAX_IDENT];
        /* Function-like if '(' immediately follows NAME (no space). */
        if (nl < restlen && rest[nl] == '(') {
            is_fn = true;
            const char *q = rest + nl + 1;
            const char *qend = rest + restlen;
            while (q < qend && *q != ')') {
                while (q < qend && (*q == ' ' || *q == ',' || *q == '\t')) q++;
                if (q >= qend) break;
                if (*q == ')') break;
                if (*q == '.') {
                    /* "..." → variadic. */
                    is_varargs_m = true;
                    while (q < qend && *q != ')' && *q != ',') q++;
                    continue;
                }
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
        m->is_varargs = is_varargs_m;
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
        g_if_parent[g_if_depth] = skipping();
        g_if_stack[g_if_depth] = g_if_parent[g_if_depth] ? 0 : (taken ? 1 : 0);
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
        g_if_parent[g_if_depth] = skipping();
        g_if_stack[g_if_depth] = g_if_parent[g_if_depth] ? 0 : (taken ? 1 : 0);
        g_if_taken[g_if_depth] = taken;
        g_if_depth++;
        return;
    }

    if (strcmp(d, "if") == 0) {
        char exprbuf[1024];
        if (restlen >= sizeof(exprbuf)) restlen = sizeof(exprbuf) - 1;
        memcpy(exprbuf, rest, restlen);
        exprbuf[restlen] = 0;
        long v = skipping() ? 0 : eval_const_expr(exprbuf);
        bool taken = (v != 0);
        if (g_if_depth >= MAX_IF_DEPTH) cc_fatal("#if nesting too deep");
        g_if_parent[g_if_depth] = skipping();
        g_if_stack[g_if_depth] = g_if_parent[g_if_depth] ? 0 : (taken ? 1 : 0);
        g_if_taken[g_if_depth] = taken;
        g_if_depth++;
        return;
    }

    if (strcmp(d, "elif") == 0) {
        if (g_if_depth <= 0) return;
        if (g_if_parent[g_if_depth - 1] || g_if_taken[g_if_depth - 1]) {
            /* Already taken a branch. */
            g_if_stack[g_if_depth - 1] = 0;
        } else {
            char exprbuf[1024];
            if (restlen >= sizeof(exprbuf)) restlen = sizeof(exprbuf) - 1;
            memcpy(exprbuf, rest, restlen);
            exprbuf[restlen] = 0;
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
        if (g_if_parent[g_if_depth - 1] || g_if_taken[g_if_depth - 1]) {
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

static long eval_const_expr(const char *expr) {
    /* Resolve defined operands before expansion, preserving identifier/token
     * boundaries and character literals. Then expand the remaining macros. */
    char buf[8192];
    size_t length = 0;
    const char *p = expr;
    while (*p && length + 1 < sizeof(buf)) {
        if (*p == '\'') {
            buf[length++] = *p++;
            while (*p && length + 2 < sizeof(buf)) {
                char c = *p++;
                buf[length++] = c;
                if (c == '\\' && *p) buf[length++] = *p++;
                else if (c == '\'') break;
            }
        } else if (is_ident_start((unsigned char)*p)) {
            const char *start = p;
            while (is_ident_char((unsigned char)*p)) p++;
            size_t size = p - start;
            if (size != 7 || strncmp(start, "defined", 7) != 0) {
                if (length + size >= sizeof(buf)) cc_fatal("#if expression too long");
                memcpy(buf + length, start, size);
                length += size;
                continue;
            }
            while (*p == ' ' || *p == '\t') p++;
            bool paren = *p == '(';
            if (paren) p++;
            while (*p == ' ' || *p == '\t') p++;
            char name[CC_MAX_IDENT];
            size_t n = 0;
            if (!is_ident_start((unsigned char)*p)) {
                cc_error("defined requires an identifier");
                return 0;
            }
            while (is_ident_char((unsigned char)*p)) {
                if (n + 1 < sizeof(name)) name[n++] = *p;
                p++;
            }
            name[n] = 0;
            while (*p == ' ' || *p == '\t') p++;
            if (paren && *p != ')') {
                cc_error("missing ')' after defined");
                return 0;
            }
            if (paren) p++;
            buf[length++] = find_macro(name) ? '1' : '0';
        } else buf[length++] = *p++;
    }
    if (*p) cc_fatal("#if expression too long");
    buf[length] = 0;
    expand_macros(buf, length, sizeof(buf));
    return pp_eval_integer(buf);
}

/* Strip comments from a logical line, replacing them with a single space.
 * `in_comment` carries the block-comment state across lines (pp is
 * line-oriented). String and char literals are respected so that
 * "http://x" or 'a' / '*' don't break the scan.
 * Returns the new length; the buffer is modified in place. */
static size_t strip_comments(char *line, size_t len, bool *in_comment) {
    size_t r = 0, w = 0;
    bool in_str = false, in_chr = false;
    while (r < len) {
        char c = line[r];
        if (*in_comment) {
            if (c == '*' && r + 1 < len && line[r + 1] == '/') {
                *in_comment = false;
                r += 2;
                /* Replace whole comment with one space (if not at start
                 * of an existing gap) so tokens don't fuse. */
                if (w > 0 && line[w - 1] != ' ' && line[w - 1] != '\t') {
                    line[w++] = ' ';
                }
            } else {
                r++;
            }
            continue;
        }
        if (in_str) {
            line[w++] = line[r];
            if (c == '\\' && r + 1 < len) {
                line[w++] = line[r + 1];
                r += 2;
                continue;
            }
            if (c == '"') in_str = false;
            r++;
            continue;
        }
        if (in_chr) {
            line[w++] = line[r];
            if (c == '\\' && r + 1 < len) {
                line[w++] = line[r + 1];
                r += 2;
                continue;
            }
            if (c == '\'') in_chr = false;
            r++;
            continue;
        }
        if (c == '"') { in_str = true; line[w++] = c; r++; continue; }
        if (c == '\'') { in_chr = true; line[w++] = c; r++; continue; }
        if (c == '/' && r + 1 < len && line[r + 1] == '*') {
            *in_comment = true;
            r += 2;
            continue;
        }
        if (c == '/' && r + 1 < len && line[r + 1] == '/') {
            /* Line comment: drop rest of line. */
            break;
        }
        line[w++] = line[r];
        r++;
    }
    /* If we ended inside a string/char (shouldn't happen on a logical
     * line), keep whatever was written. */
    return w;
}

/* 1: trailing macro name might be followed by '('; 2: open invocation.
 * Scan tokens rather than counting raw parentheses (strings can contain ')'). */
static int pending_invocation(const char *text, size_t length) {
    const char *p = text, *end = text + length;
    static char args[CC_MAX_MACRO_ARGS + 8][512];
    while (p < end) {
        if (*p == '"' || *p == '\'') {
            char quote = *p++;
            while (p < end && *p != quote) {
                if (*p == '\\' && p + 1 < end) p++;
                p++;
            }
            if (p < end) p++;
        } else if (is_ident_start((unsigned char)*p)) {
            char name[CC_MAX_IDENT];
            size_t n = 0;
            while (p < end && is_ident_char((unsigned char)*p)) {
                if (n + 1 < sizeof(name)) name[n++] = *p;
                p++;
            }
            name[n] = 0;
            macro_t *macro = find_macro(name);
            /* Object aliases can become function-like on the next pass. */
            for (int depth = 0; macro && !macro->is_function_like && depth < 16; depth++) {
                const char *body = macro->body;
                while (isspace((unsigned char)*body)) body++;
                size_t count = 0;
                while (is_ident_char((unsigned char)*body) && count + 1 < sizeof(name))
                    name[count++] = *body++;
                name[count] = 0;
                while (isspace((unsigned char)*body)) body++;
                macro = count && !*body ? find_macro(name) : NULL;
            }
            if (!macro || !macro->is_function_like) continue;
            const char *next = p;
            while (next < end && isspace((unsigned char)*next)) next++;
            if (next == end) return 1;
            if (*next == '(' && read_macro_args(&next, end, args, CC_MAX_MACRO_ARGS + 8) < 0)
                return 2;
        } else p++;
    }
    return 0;
}

static void process_source(const char *src, size_t len, const char *filename) {
    const char *p = src;
    const char *end = src + len;
    int lineno = 1;
    bool in_comment = false;
    while (p < end) {
        /* Find end of line. */
        const char *lend = memchr(p, '\n', end - p);
        if (!lend) lend = end;
        size_t llen = lend - p;

        /* Strip trailing \r. */
        while (llen > 0 && (p[llen - 1] == '\r' || p[llen - 1] == '\n')) llen--;

        /* Physical → logical line: join backslash continuations. */
        char linebuf[8192];
        size_t qlen = llen;
        if (qlen >= sizeof(linebuf)) qlen = sizeof(linebuf) - 1;
        memcpy(linebuf, p, qlen);
        linebuf[qlen] = 0;
        int joined_lines = 0;
        while (qlen > 0 && linebuf[qlen - 1] == '\\' &&
               lend < end && joined_lines < 64) {
            /* Remove the backslash, join next physical line. */
            qlen--;
            const char *nl = memchr(lend + 1, '\n', end - (lend + 1));
            if (!nl) nl = end;
            size_t nlen = nl - (lend + 1);
            while (nlen > 0 && (*(lend + 1 + nlen - 1) == '\r')) nlen--;
            if (qlen + nlen >= sizeof(linebuf)) nlen = sizeof(linebuf) - 1 - qlen;
            memcpy(linebuf + qlen, lend + 1, nlen);
            qlen += nlen;
            linebuf[qlen] = 0;
            joined_lines++;
            lend = nl;
        }

        /* Strip comments BEFORE macro expansion — a macro body must never
         * be expanded inside a comment (a macro body containing the
         * comment-terminator sequence used to inject stray tokens into
         * comments mentioning that macro). */
        char cbuf[8192];
        size_t clen = 0;
        if (qlen >= sizeof(cbuf)) qlen = sizeof(cbuf) - 1;
        memcpy(cbuf, linebuf, qlen);
        clen = strip_comments(cbuf, qlen, &in_comment);
        cbuf[clen] = 0;

        /* Gather the token stream of invocations spanning physical lines.
         * Directives remain separate and are never consumed as arguments. */
        const char *first = cbuf;
        while (isspace((unsigned char)*first)) first++;
        if (*first != '#' && !skipping()) {
            int pending;
            while ((pending = pending_invocation(cbuf, clen)) && lend < end) {
                const char *next = lend + 1;
                const char *next_end = memchr(next, '\n', end - next);
                if (!next_end) next_end = end;
                char continuation[8192];
                size_t count = next_end - next;
                if (count >= sizeof(continuation)) {
                    cc_error_at(filename, lineno, "macro invocation line too long");
                    break;
                }
                memcpy(continuation, next, count);
                while (count && continuation[count - 1] == '\r') count--;
                int extra_lines = 0;
                while (count && continuation[count - 1] == '\\' && next_end < end) {
                    count--;
                    const char *more = next_end + 1;
                    next_end = memchr(more, '\n', end - more);
                    if (!next_end) next_end = end;
                    size_t more_count = next_end - more;
                    while (more_count && more[more_count - 1] == '\r') more_count--;
                    if (count + more_count >= sizeof(continuation)) {
                        cc_error_at(filename, lineno, "macro invocation line too long");
                        more_count = sizeof(continuation) - count - 1;
                    }
                    memcpy(continuation + count, more, more_count);
                    count += more_count;
                    extra_lines++;
                }
                bool next_comment = in_comment;
                count = strip_comments(continuation, count, &next_comment);
                continuation[count] = 0;
                const char *token = continuation;
                while (isspace((unsigned char)*token)) token++;
                if (*token == '#') {
                    if (pending == 2)
                        cc_error_at(filename, lineno, "directive inside unfinished macro invocation");
                    break;
                }
                if (clen + count + 1 >= sizeof(cbuf)) {
                    cc_error_at(filename, lineno, "macro invocation too long");
                    break;
                }
                cbuf[clen++] = '\n';
                memcpy(cbuf + clen, continuation, count);
                clen += count;
                cbuf[clen] = 0;
                in_comment = next_comment;
                joined_lines += 1 + extra_lines;
                lend = next_end;
            }
        }

        /* Directive? */
        const char *q = cbuf;
        size_t qlen2 = clen;
        while (qlen2 > 0 && (*q == ' ' || *q == '\t')) { q++; qlen2--; }
        if (qlen2 > 0 && q[0] == '#') {
            handle_directive(q + 1, qlen2 - 1, filename, lineno);
        } else if (!skipping()) {
            /* Expand macros in non-directive lines. */
            if (clen < sizeof(linebuf)) {
                memcpy(linebuf, cbuf, clen);
                linebuf[clen] = 0;
                size_t nl = expand_macros(linebuf, clen, sizeof(linebuf));
                /* Substitute __ONYX_FILE__ (from __FILE__) with the quoted
                 * current filename — but ONLY outside string/char literals
                 * (the compiler's own source contains the literal token
                 * inside strstr("__ONYX_FILE__") which must survive). */
                char fileq[CC_MAX_PATH + 4];
                if (filename && strstr(linebuf, "__ONYX_FILE__")) {
                    size_t fl = strlen(filename);
                    if (fl > CC_MAX_PATH - 3) fl = CC_MAX_PATH - 3;
                    fileq[0] = '"';
                    memcpy(fileq + 1, filename, fl);
                    fileq[1 + fl] = '"';
                    fileq[2 + fl] = 0;
                    /* Scan into a separate output buffer with capacity
                     * checks (in-place writing could overflow when the
                     * filename is longer than the 13-char token). */
                    static char sub[16384];
                    size_t wl = 0;
                    char *r = linebuf;
                    bool instr = false, inchr = false;
                    bool overflow = false;
                    while (*r && !overflow) {
                        char c = *r;
                        if (instr) {
                            if (c == '\\' && r[1]) {
                                if (wl + 2 >= sizeof(sub)) { overflow = true; break; }
                                sub[wl++] = *r++; sub[wl++] = *r++;
                                continue;
                            }
                            if (c == '"') instr = false;
                            if (wl + 1 >= sizeof(sub)) { overflow = true; break; }
                            sub[wl++] = *r++;
                            continue;
                        }
                        if (inchr) {
                            if (c == '\\' && r[1]) {
                                if (wl + 2 >= sizeof(sub)) { overflow = true; break; }
                                sub[wl++] = *r++; sub[wl++] = *r++;
                                continue;
                            }
                            if (c == '\'') inchr = false;
                            if (wl + 1 >= sizeof(sub)) { overflow = true; break; }
                            sub[wl++] = *r++;
                            continue;
                        }
                        if (c == '"') { instr = true; }
                        if (c == '\'') { inchr = true; }
                        if (strncmp(r, "__ONYX_FILE__", 13) == 0) {
                            size_t ql = strlen(fileq);
                            if (wl + ql >= sizeof(sub)) { overflow = true; break; }
                            memcpy(sub + wl, fileq, ql);
                            wl += ql;
                            r += 13;
                            continue;
                        }
                        if (wl + 1 >= sizeof(sub)) { overflow = true; break; }
                        sub[wl++] = *r++;
                    }
                    if (!overflow && wl < sizeof(linebuf)) {
                        sub[wl] = 0;
                        memcpy(linebuf, sub, wl + 1);
                        nl = wl;
                    }
                    /* On overflow keep the un-substituted line — the lexer
                     * will surface a sane diagnostic instead of crashing. */
                }
                out_emit(linebuf, nl);
                int newlines = 0;
                for (size_t i = 0; i < nl; i++) if (linebuf[i] == '\n') newlines++;
                while (newlines++ < joined_lines) out_emit("\n", 1);
            } else {
                out_emit(cbuf, clen);
            }
            out_emit("\n", 1);
        } else {
            /* Emit empty line to preserve line numbers. */
            out_emit("\n", 1);
        }
        p = (lend < end) ? lend + 1 : end;
        lineno += 1 + joined_lines;
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
    /* __FILE__ / __LINE__: updated per line in process_source before
     * expansion (see the __FILE__ handling there). Define dummies so
     * `defined(__FILE__)` is true; the actual substitution happens inline. */
    define_macro("__FILE__=__ONYX_FILE__");
    define_macro("__LINE__=0");
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
