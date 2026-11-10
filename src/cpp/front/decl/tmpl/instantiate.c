/*
 * decl/tmpl/instantiate.c — substitution and deferred instantiation.
 */
#include "core/compat.h"

#include "core/cc.h"
#include "core/types.h"
#include "front/lexer.h"
#include "front/ast.h"
#include "front/parser_priv.h"
#include "back/gen.h"
#include "arch/riscv64.h"
#include "cpp/cpp_front.h"
#include "cpp/cpp.h"

static size_t app(char *out, size_t len, const char *s) {
    while (*s) out[len++] = *s++;
    return len;
}

bool cpp_template_instantiate(lexer_t *lx, const char *name, val_t *out) {
    int ti = cpp_template_find(name);
    if (ti < 0) return false;
    cpp_template_t *t = &g_templates[ti];
    lex_next(lx);                       /* name */
    if (lx->cur.kind != T_LT) return false;
    lex_next(lx);

    char spell[CPP_MAX_TPARAMS][CC_MAX_IDENT * 2];
    char codes[CPP_MAX_TPARAMS * (CC_MAX_IDENT * 2 + 1)];
    memset(spell, 0, sizeof(spell));
    memset(codes, 0, sizeof(codes));
    int nargs = 0;
    while (lx->cur.kind != T_GT && lx->cur.kind != T_EOF) {
        if (nargs >= CPP_MAX_TPARAMS) {
            cc_error("too many template arguments");
            return false;
        }
        size_t len = 0;
        while (lx->cur.kind != T_COMMA && lx->cur.kind != T_GT &&
               lx->cur.kind != T_EOF) {
            const char *txt = lx->cur.text;
            if (!txt[0]) txt = tok_kind_str(lx->cur.kind);
            size_t l = strlen(txt);
            if (len + l + 2 >= sizeof(spell[0])) break;
            if (len) spell[nargs][len++] = ' ';
            memcpy(spell[nargs] + len, txt, l);
            len += l;
            lex_next(lx);
        }
        spell[nargs][len] = 0;
        if (!spell[nargs][0]) {
            cc_error("empty template argument");
            return false;
        }
        snprintf(codes + strlen(codes), sizeof(codes) - strlen(codes),
                 "%s_", spell[nargs]);
        nargs++;
        if (accept(T_COMMA)) continue;
        break;
    }
    if (lx->cur.kind != T_GT) {
        cc_error("expected '>' after template arguments");
        return false;
    }
    lex_next(lx);
    if (nargs != t->nparams) {
        cc_error("template '%s' expects %d type argument(s), got %d",
                 name, t->nparams, nargs);
        return false;
    }

    unsigned h = 2166136261u;
    for (size_t i = 0; codes[i]; i++) {
        h ^= (unsigned char)codes[i];
        h *= 16777619u;
    }
    char inst[CC_MAX_IDENT];
    snprintf(inst, sizeof(inst), "%s__t%08x", name, h & 0xffffffffu);
    if (strlen(inst) >= CC_MAX_IDENT - 1) {
        cc_error("instantiated name too long");
        return false;
    }

    size_t span = t->end_off - t->start_off;
    size_t cap = span + (size_t)t->nparams * (CC_MAX_IDENT * 2 + 2) + 256;
    char *buf = (char *)malloc(cap);
    if (!buf) cc_fatal("out of memory");
    size_t len = 0;
    const char *p = lx->src + t->start_off;
    const char *endp = lx->src + t->end_off;
    while (p < endp) {
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || *p == '_') {
            const char *w = p;
            while (p < endp && ((*p >= 'A' && *p <= 'Z') ||
                                (*p >= 'a' && *p <= 'z') ||
                                (*p >= '0' && *p <= '9') || *p == '_')) p++;
            size_t wl = (size_t)(p - w);
            bool replaced = false;
            for (int i = 0; i < t->nparams; i++) {
                if (strlen(t->params[i]) == wl &&
                    strncmp(t->params[i], w, wl) == 0) {
                    len = app(buf, len, spell[i]);
                    replaced = true;
                    break;
                }
            }
            if (!replaced && strlen(t->name) == wl &&
                strncmp(t->name, w, wl) == 0)
                len = app(buf, len, inst);
            if (!replaced && (strlen(t->name) != wl ||
                              strncmp(t->name, w, wl) != 0))
                { memcpy(buf + len, w, wl); len += wl; }
            if (len + (size_t)(endp - p) + 2 > cap) break;
        } else {
            buf[len++] = *p++;
        }
    }
    buf[len] = 0;

    /* Compiling the body here would splice another function's code into
     * the middle of the current one. Install a prototype now (so the call
     * binds) and QUEUE the body for compilation at top level after the
     * current declaration finishes. */
    size_t sig_end = 0;
    {
        size_t paren = 0;
        bool seen = false;
        int depth = 0;
        for (size_t i = 0; i < len; i++) {
            if (buf[i] == '(') { depth++; seen = true; }
            else if (buf[i] == ')') {
                depth--;
                if (seen && depth == 0) { sig_end = i + 1; break; }
            }
        }
    }
    if (!sig_end) {
        cc_error("malformed template body for '%s'", name);
        free(buf);
        out->kind = VAL_IMM;
        out->type = &ty_int;
        return true;
    }
    char *proto = (char *)malloc(sig_end + 2);
    if (!proto) cc_fatal("out of memory");
    memcpy(proto, buf, sig_end);
    proto[sig_end] = ';';
    proto[sig_end + 1] = 0;

    lexer_t pl;
    lex_init(&pl, proto, sig_end + 1, "<template prototype>");
    lexer_t *saved = g_lx;
    parser_set_lexer(&pl);
    parse_top_level_decl();
    parser_set_lexer(saved);
    free(proto);

    int idx = gen_lookup_global(inst);
    if (idx < 0) {
        cc_error("failed to instantiate template '%s'", name);
        free(buf);
        out->kind = VAL_IMM;
        out->type = &ty_int;
        return true;
    }
    cpp_queue_pending_template(buf, len);
    memset(out, 0, sizeof(*out));
    out->kind = VAL_SYM;
    out->sym_idx = idx;
    out->type = g_globals[idx].type;
    return true;
}

/* ── Deferred compilation of instantiated bodies ────────────────────── */

typedef struct {
    char *buf;
    size_t len;
} cpp_pending_t;

#define CPP_MAX_PENDING 64
static cpp_pending_t g_pending[CPP_MAX_PENDING];
static int g_n_pending;
static int g_pending_head;

void cpp_queue_pending_template(char *buf, size_t len) {
    if (g_n_pending >= CPP_MAX_PENDING)
        cc_fatal("too many pending template instantiations");
    g_pending[g_n_pending].buf = buf;
    g_pending[g_n_pending].len = len;
    g_n_pending++;
}

void cpp_drain_pending_templates(void) {
    while (g_pending_head < g_n_pending) {
        cpp_pending_t *e = &g_pending[g_pending_head++];
        lexer_t nl;
        lex_init(&nl, e->buf, e->len, "<template body>");
        lexer_t *saved = g_lx;
        parser_set_lexer(&nl);
        parse_top_level_decl();
        parser_set_lexer(saved);
        free(e->buf);
        e->buf = NULL;
    }
    g_n_pending = 0;
    g_pending_head = 0;
}

