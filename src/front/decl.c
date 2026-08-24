/*
 * decl.c — full C declarator parser (function pointers, arrays of them,
 *          abstract declarators, struct fields with callbacks).
 *
 * Uses the "splice" algorithm: a parenthesized declarator is first parsed
 * with a NULL base type; the suffix chain that follows the ')' is built
 * on the real base; finally the NULL bottom inside the inner chain is
 * replaced (spliced) with the suffix type.
 *
 * Example: int (*fp)(void)
 *   base = int
 *   inner declarator "*fp"   → chain: ptr(NULL), name = "fp"
 *   suffix "(void)"          → FUNC(params=void, ret=int)
 *   splice(ptr(NULL), FUNC)  → ptr(FUNC(void → int))   ✓
 */
#include "core/compat.h"

#include "core/cc.h"
#include "front/lexer.h"
#include "core/types.h"
#include "front/ast.h"
#include "front/decl.h"
#include "front/parser_priv.h"
#include "back/gen.h"     /* parse_decl_spec, is_type_start */

/* ── Splice ─────────────────────────────────────────────────────────── */

type_t *type_splice_base(type_t *t, type_t *base) {
    if (!t) return base;
    switch (t->kind) {
        case TY_PTR:
        case TY_ARRAY:
            t->base = type_splice_base(t->base, base);
            return t;
        case TY_FUNC:
            t->ret = type_splice_base(t->ret, base);
            return t;
        default:
            /* A complete type already — nothing to splice (shouldn't
             * happen when the chain was built on NULL). */
            return t;
    }
}

/* ── Parameter list ─────────────────────────────────────────────────── */

void parse_param_list(lexer_t *lx, type_t *ftype) {
    ftype->nparams = 0;
    ftype->is_varargs = false;

    if (lx->cur.kind == T_RPAREN) return;   /* () — old-style, no proto */

    for (;;) {
        if (lx->cur.kind == T_ELLIPSIS) {
            ftype->is_varargs = true;
            lex_next(lx);
            break;
        }

        type_t *pbase = NULL;
        bool ps, pe, pt, pi;
        char ptag[CC_MAX_IDENT] = {0};
        if (!parse_decl_spec(lx, &pbase, &ps, &pe, &pt, &pi, ptag)) {
            parse_error("expected parameter type");
            break;
        }

        char pname[CC_MAX_IDENT] = {0};
        type_t *ptyp = parse_declarator(lx, pbase, pname, sizeof(pname));

        /* Array parameters decay to pointers. */
        if (ptyp->kind == TY_ARRAY) ptyp = type_decay(ptyp);
        /* Bare function parameters decay to function pointers. */
        if (ptyp->kind == TY_FUNC)  ptyp = type_make_ptr(ptyp);

        if (ftype->nparams < CC_MAX_FUNC_PARAMS) {
            func_param_t *pp = &ftype->params[ftype->nparams++];
            strncpy(pp->name, pname, CC_MAX_IDENT - 1);
            pp->name[CC_MAX_IDENT - 1] = 0;
            pp->type = ptyp;
        }
        if (!accept(T_COMMA)) break;
    }
}

/* ── Suffix chain ───────────────────────────────────────────────────── */

type_t *parse_type_suffix(lexer_t *lx, type_t *base) {
    if (lx->cur.kind == T_LPAREN) {
        lex_next(lx);
        type_t *ft = (type_t *)cc_arena_alloc(&g_type_arena, sizeof(type_t), 8);
        memset(ft, 0, sizeof(*ft));
        ft->kind = TY_FUNC;
        ft->ret = base;
        ft->size = 8;
        ft->align = 8;
        ft->is_complete = true;
        parse_param_list(lx, ft);
        parse_expect(T_RPAREN, "expected ')' after parameter list");
        /* (void) means no parameters. */
        if (ft->nparams == 1 && ft->params[0].type == &ty_void &&
            ft->params[0].name[0] == 0) {
            ft->nparams = 0;
        }
        /* A function cannot return a function/array directly — in practice
         * a suffix after (params) only appears in declarators like
         * (*f)(int)(char), where it applies to the inner chain. */
        return parse_type_suffix(lx, ft);
    }
    if (lx->cur.kind == T_LBRACKET) {
        lex_next(lx);
        uint64_t len = 0;
        if (lx->cur.kind != T_RBRACKET) {
            len = (uint64_t)parse_const_expr();
        }
        parse_expect(T_RBRACKET, "expected ']'");
        /* Remaining suffixes bind tighter: in `int a[3][4]` the [4] applies
         * to the element type, [3] is the outer dimension. Parse the rest
         * first, then wrap. (The old parser nested dimensions in the wrong
         * order, breaking non-square multi-dim arrays.) */
        type_t *rest = parse_type_suffix(lx, base);
        return type_make_array(rest, len);
    }
    return base;
}

/* ── Full declarator ────────────────────────────────────────────────── */

type_t *parse_declarator(lexer_t *lx, type_t *base,
                         char *out_name, size_t name_sz) {
    if (out_name && name_sz > 0) out_name[0] = 0;

    type_t *t = base;
    /* Leading pointer stars. */
    for (;;) {
        if (accept(T_STAR)) {
            t = type_make_ptr(t);
        } else if (lx->cur.kind == T_KW_CONST || lx->cur.kind == T_KW_VOLATILE ||
                   lx->cur.kind == T_KW_RESTRICT) {
            /* Qualifier attached to the pointer: const char *const p */
            lex_next(lx);
        } else {
            break;
        }
    }

    if (lx->cur.kind == T_LPAREN) {
        /* Grouped declarator: (*name) / (*name)[3] / (*name)(params). */
        /* Heuristic to disambiguate a grouped declarator from a function
         * suffix: a '(' at declarator start can ONLY be a group (a suffix
         * would need a name or a preceding ')' first). */
        lex_next(lx);
        type_t *inner = parse_declarator(lx, NULL, out_name, name_sz);
        parse_expect(T_RPAREN, "expected ')' in declarator");
        type_t *suf = parse_type_suffix(lx, t);
        if (inner == NULL) {
            /* Abstract inner like "(*)": type is the suffix applied to t,
             * except t itself may have pointer layers — e.g. (int *)(*pf)
             * is invalid; for (*) the star is inside the group. */
            return suf;
        }
        return type_splice_base(inner, suf);
    }

    if (lx->cur.kind == T_IDENT) {
        if (out_name && name_sz > 0) {
            strncpy(out_name, lx->cur.text, name_sz - 1);
            out_name[name_sz - 1] = 0;
        }
        lex_next(lx);
    }

    return parse_type_suffix(lx, t);
}
