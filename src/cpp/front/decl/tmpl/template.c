/*
 * decl/tmpl/template.c — function-template records (Phase F).
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

void cpp_queue_pending_template(char *buf, size_t len);

cpp_template_t g_templates[CPP_MAX_TEMPLATES];
int g_n_templates;

int cpp_template_find(const char *name) {
    for (int i = g_n_templates - 1; i >= 0; i--)
        if (strcmp(g_templates[i].name, name) == 0) return i;
    return -1;
}

/* Skip one complete declaration from `t` (a lexer copy); returns the end
 * byte offset. */
static size_t scan_decl_end(lexer_t *t) {
    int depth = 0;
    for (;;) {
        if (t->cur.kind == T_EOF) return t->cur.src_off;
        if (t->cur.kind == T_LBRACE) {
            depth++;
        } else if (t->cur.kind == T_RBRACE) {
            depth--;
            if (depth <= 0) {
                lex_next(t);
                return t->cur.src_off;
            }
        } else if (t->cur.kind == T_SEMI && depth == 0) {
            lex_next(t);
            return t->cur.src_off;
        }
        lex_next(t);
    }
}

/* First identifier directly followed by '(' inside the span — the
 * declared function name. */
static bool find_fn_name(lexer_t *t, size_t end, char *out, size_t out_sz) {
    (void)end;
    for (;;) {
        if (t->cur.kind == T_EOF) return false;
        if (t->cur.kind == T_IDENT) {
            char name[CC_MAX_IDENT];
            strncpy(name, t->cur.text, CC_MAX_IDENT - 1);
            name[CC_MAX_IDENT - 1] = 0;
            lexer_t probe = *t;
            lex_next(&probe);
            if (probe.cur.kind == T_LPAREN) {
                strncpy(out, name, out_sz - 1);
                out[out_sz - 1] = 0;
                return true;
            }
        }
        lex_next(t);
    }
}

bool cpp_parse_template(lexer_t *lx) {
    lex_next(lx);                       /* template */
    if (lx->cur.kind != T_LT) {
        parse_error("expected '<' after 'template'");
        cpp_skip_decl(lx);
        return true;
    }
    lex_next(lx);
    char params[CPP_MAX_TPARAMS][CC_MAX_IDENT];
    int nparams = 0;
    while (lx->cur.kind != T_GT && lx->cur.kind != T_EOF) {
        if (lx->cur.kind == T_KW_CLASS || lx->cur.kind == T_KW_TYPENAME) {
            lex_next(lx);
            if (lx->cur.kind != T_IDENT) {
                parse_error("expected template parameter name");
                cpp_skip_decl(lx);
                return true;
            }
            if (nparams < CPP_MAX_TPARAMS) {
                strncpy(params[nparams], lx->cur.text, CC_MAX_IDENT - 1);
                params[nparams][CC_MAX_IDENT - 1] = 0;
                nparams++;
            }
            lex_next(lx);
        } else {
            cc_error("non-type template parameters are not supported");
            cpp_skip_decl(lx);
            return true;
        }
        if (accept(T_COMMA)) continue;
        break;
    }
    if (lx->cur.kind != T_GT) {
        parse_error("expected '>' after template parameters");
        cpp_skip_decl(lx);
        return true;
    }
    lex_next(lx);

    size_t start = lx->cur.src_off;
    lexer_t scan = *lx;
    size_t end = scan_decl_end(&scan);
    if (end <= start) {
        cc_error("unable to capture template body");
        *lx = scan;
        return true;
    }
    char fname[CC_MAX_IDENT] = {0};
    lexer_t probe = *lx;
    if (!find_fn_name(&probe, end, fname, sizeof(fname))) {
        cc_error("class templates / non-function templates are not supported");
        *lx = scan;
        return true;
    }
    if (g_n_templates >= CPP_MAX_TEMPLATES)
        cc_fatal("too many function templates");
    cpp_template_t *t = &g_templates[g_n_templates++];
    memset(t, 0, sizeof(*t));
    strncpy(t->name, fname, CC_MAX_IDENT - 1);
    t->nparams = nparams;
    for (int i = 0; i < nparams; i++)
        strncpy(t->params[i], params[i], CC_MAX_IDENT - 1);
    t->start_off = start;
    t->end_off = end;
    *lx = scan;                         /* skip the template definition */
    return true;
}

