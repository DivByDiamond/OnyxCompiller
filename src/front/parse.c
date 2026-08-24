/*
 * parse.c — top-level declaration parser.
 *
 * Drives the translation unit: parses one declaration at a time and
 * invokes gen_decl() to emit code. Functions get their body parsed
 * inside gen.c (gen_func_body) to keep parser/gen interleaving tight.
 */
#include "core/compat.h"

#include "core/cc.h"
#include "front/lexer.h"
#include "core/types.h"
#include "front/ast.h"
#include "front/parse.h"
#include "back/gen.h"
#include "front/parser_priv.h"

lexer_t *g_lx = NULL;

void parser_set_lexer(lexer_t *lx) { g_lx = lx; }

void parse_error(const char *msg) {
    cc_error_at(g_lx->cur.pos.file, g_lx->cur.pos.line,
                "parse: %s (got %s '%s')",
                msg, tok_kind_str(g_lx->cur.kind), g_lx->cur.text);
}

void parse_expect(token_kind_t k, const char *what) {
    if (g_lx->cur.kind != k) {
        parse_error(what);
    }
    lex_next(g_lx);
}

bool accept(token_kind_t k) {
    if (g_lx->cur.kind == k) { lex_next(g_lx); return true; }
    return false;
}

/* ---- Constant expression evaluator (for array sizes, etc.) ---------- */

static int const_op_prec(token_kind_t k) {
    switch (k) {
        case T_OR: return 1;
        case T_AND: return 2;
        case T_PIPE: return 3;
        case T_CARET: return 4;
        case T_AMP: return 5;
        case T_EQ: case T_NE: return 6;
        case T_LT: case T_GT: case T_LE: case T_GE: return 7;
        case T_SHL: case T_SHR: return 8;
        case T_PLUS: case T_MINUS: return 9;
        case T_STAR: case T_SLASH: case T_PERCENT: return 10;
        default: return 0;
    }
}

static long long const_expr_prim(void);
static long long const_expr_binop(int min_prec);

static long long const_expr_unary(void) {
    if (accept(T_PLUS)) return const_expr_unary();
    if (accept(T_MINUS)) return -const_expr_unary();
    if (accept(T_NOT)) return !const_expr_unary();
    if (accept(T_TILDE)) return ~const_expr_unary();
    if (accept(T_LPAREN)) {
        long long v = const_expr_binop(0);
        parse_expect(T_RPAREN, "expected ')'");
        return v;
    }
    if (g_lx->cur.kind == T_INT || g_lx->cur.kind == T_LONG ||
        g_lx->cur.kind == T_UINT || g_lx->cur.kind == T_ULONG) {
        long long v = (long long)g_lx->cur.ival;
        lex_next(g_lx);
        return v;
    }
    if (g_lx->cur.kind == T_IDENT) {
        /* Try enum constant lookup. */
        int idx = symtab_lookup_global(g_lx->cur.text);
        lex_next(g_lx);
        if (idx >= 0 && g_globals[idx].kind == SYM_ENUM_CONST) {
            return (long long)g_globals[idx].enum_val;
        }
        /* Otherwise treat as 0 and emit error? For array sizes this will
         * likely lead to further errors; return 0 to keep parsing. */
        return 0;
    }
    parse_error("expected constant expression");
    return 0;
}

static long long const_expr_binop(int min_prec) {
    long long lhs = const_expr_unary();
    for (;;) {
        int prec = const_op_prec(g_lx->cur.kind);
        if (prec < min_prec || prec == 0) break;
        token_kind_t op = g_lx->cur.kind;
        lex_next(g_lx);
        long long rhs = const_expr_binop(prec + 1);
        switch (op) {
            case T_PLUS: lhs = lhs + rhs; break;
            case T_MINUS: lhs = lhs - rhs; break;
            case T_STAR: lhs = lhs * rhs; break;
            case T_SLASH: lhs = rhs ? lhs / rhs : 0; break;
            case T_PERCENT: lhs = rhs ? lhs % rhs : 0; break;
            case T_SHL: lhs = lhs << rhs; break;
            case T_SHR: lhs = lhs >> rhs; break;
            case T_AMP: lhs = lhs & rhs; break;
            case T_PIPE: lhs = lhs | rhs; break;
            case T_CARET: lhs = lhs ^ rhs; break;
            case T_AND: lhs = lhs && rhs; break;
            case T_OR: lhs = lhs || rhs; break;
            case T_EQ: lhs = (lhs == rhs); break;
            case T_NE: lhs = (lhs != rhs); break;
            case T_LT: lhs = (lhs < rhs); break;
            case T_GT: lhs = (lhs > rhs); break;
            case T_LE: lhs = (lhs <= rhs); break;
            case T_GE: lhs = (lhs >= rhs); break;
            default: break;
        }
    }
    return lhs;
}

long long parse_const_expr(void) {
    return const_expr_binop(0);
}

/* Skip __attribute__((...)) or asm(...). No-op if current token is not
 * an identifier matching those names. */
void skip_attributes(void) {
    while (g_lx->cur.kind == T_IDENT &&
           (strcmp(g_lx->cur.text, "__attribute__") == 0 ||
            strcmp(g_lx->cur.text, "__attribute") == 0 ||
            strcmp(g_lx->cur.text, "asm") == 0 ||
            strcmp(g_lx->cur.text, "__asm__") == 0)) {
        lex_next(g_lx);  /* consume the identifier */
        /* Expect one or more parenthesized groups. */
        while (accept(T_LPAREN)) {
            int depth = 1;
            while (depth > 0 && g_lx->cur.kind != T_EOF) {
                if (g_lx->cur.kind == T_LPAREN) depth++;
                else if (g_lx->cur.kind == T_RPAREN) depth--;
                lex_next(g_lx);
            }
        }
    }
}

/* Parse a top-level declaration. */
static void parse_top_level(void) {
    type_t *base = NULL;
    bool is_static = false, is_extern = false, is_typedef = false, is_inline = false;
    char tag[CC_MAX_IDENT] = {0};
    if (!parse_decl_spec(g_lx, &base, &is_static, &is_extern, &is_typedef, &is_inline, tag)) {
        if (accept(T_SEMI)) return;
        parse_error("expected declaration");
        lex_next(g_lx);
        return;
    }

    if (g_lx->cur.kind == T_SEMI) {
        lex_next(g_lx);
        return;
    }

    for (;;) {
        type_t *t = base;
        while (accept(T_STAR)) {
            t = type_make_ptr(t);
            while (accept(T_KW_CONST) || accept(T_KW_VOLATILE)) { }
        }
        if (g_lx->cur.kind != T_IDENT) {
            parse_error("expected identifier");
            return;
        }
        char name[CC_MAX_IDENT];
        strncpy(name, g_lx->cur.text, CC_MAX_IDENT - 1);
        name[CC_MAX_IDENT - 1] = 0;
        cc_pos_t pos = g_lx->cur.pos;
        lex_next(g_lx);

        if (g_lx->cur.kind == T_LPAREN) {
            lex_next(g_lx);
            type_t *ftype = type_dup(t);
            ftype->kind = TY_FUNC;
            ftype->ret = t;
            ftype->nparams = 0;
            ftype->is_varargs = false;
            ftype->size = 8;
            ftype->align = 8;
            ftype->is_complete = true;

            if (g_lx->cur.kind != T_RPAREN) {
                for (;;) {
                    if (g_lx->cur.kind == T_ELLIPSIS) {
                        ftype->is_varargs = true;
                        lex_next(g_lx);
                        break;
                    }
                    type_t *pbase = NULL;
                    bool ps, pe, pt, pi;
                    char ptag[CC_MAX_IDENT] = {0};
                    if (!parse_decl_spec(g_lx, &pbase, &ps, &pe, &pt, &pi, ptag)) {
                        parse_error("expected parameter type");
                        break;
                    }
                    type_t *ptyp = pbase;
                    while (accept(T_STAR)) {
                        ptyp = type_make_ptr(ptyp);
                        /* Qualifiers between/after pointer stars: const char *const *p */
                        while (accept(T_KW_CONST) || accept(T_KW_VOLATILE)) { }
                    }
                    char pname[CC_MAX_IDENT] = {0};
                    /* Parameter name is optional in prototypes. */
                    if (g_lx->cur.kind == T_IDENT) {
                        strncpy(pname, g_lx->cur.text, CC_MAX_IDENT - 1);
                        /* But make sure it's not the start of the next param type. */
                        /* (For MVP we accept any ident as the name.) */
                        lex_next(g_lx);
                    }
                    /* Array declarator after name: argv[] */
                    while (g_lx->cur.kind == T_LBRACKET) {
                        lex_next(g_lx);
                        uint64_t len = 0;
                        if (g_lx->cur.kind != T_RBRACKET) {
                            len = (uint64_t)parse_const_expr();
                        }
                        parse_expect(T_RBRACKET, "expected ']'");
                        ptyp = type_make_array(ptyp, len);
                    }
                    /* Qualifiers may also appear after array declarator. */
                    while (accept(T_KW_CONST) || accept(T_KW_VOLATILE)) { }
                    /* Array param decays to pointer. */
                    if (ptyp->kind == TY_ARRAY) ptyp = type_decay(ptyp);
                    if (ptyp->kind == TY_FUNC)  ptyp = type_make_ptr(ptyp);
                    if (ptyp == &ty_void && pname[0] == 0) {
                        /* (void) — will be detected after the loop. */
                    }
                    if (ftype->nparams < CC_MAX_FUNC_PARAMS) {
                        func_param_t *pp = &ftype->params[ftype->nparams++];
                        strncpy(pp->name, pname, CC_MAX_IDENT - 1);
                        pp->type = ptyp;
                    }
                    if (!accept(T_COMMA)) break;
                }
            }
            parse_expect(T_RPAREN, "expected ')' after parameter list");

            /* (void) means no parameters. */
            if (ftype->nparams == 1 && ftype->params[0].type == &ty_void &&
                ftype->params[0].name[0] == 0) {
                ftype->nparams = 0;
            }

            skip_attributes();

            if (g_lx->cur.kind == T_LBRACE) {
                decl_t *d = ast_new_decl(D_FUNC_DEF, pos);
                d->type = ftype;
                strncpy(d->name, name, CC_MAX_IDENT - 1);
                if (is_static) ftype->is_static = true;
                if (is_inline) ftype->is_inline = true;
                gen_decl(d);
                return;
            } else {
                decl_t *d = ast_new_decl(D_FUNC_DECL, pos);
                d->type = ftype;
                strncpy(d->name, name, CC_MAX_IDENT - 1);
                if (is_static) ftype->is_static = true;
                gen_decl(d);
            }
        } else {
            while (g_lx->cur.kind == T_LBRACKET) {
                lex_next(g_lx);
                uint64_t len = 0;
                if (g_lx->cur.kind != T_RBRACKET) {
                    len = (uint64_t)parse_const_expr();
                }
                parse_expect(T_RBRACKET, "expected ']'");
                t = type_make_array(t, len);
            }
            expr_t *init = NULL;
            bool has_init = false;
            if (accept(T_ASSIGN)) {
                has_init = true;
                init = gen_parse_global_init(g_lx, t);
            }
            decl_t *d = ast_new_decl(D_VAR, pos);
            d->type = t;
            strncpy(d->name, name, CC_MAX_IDENT - 1);
            d->init = init;
            if (is_static) t->is_static = true;
            if (is_extern)  t->is_extern = true;
            if (is_typedef) {
                d->kind = D_TYPEDEF;
                t->is_typedef = true;
            }
            /* For globals with init, mark is_defined so finalize puts them in .data. */
            if (has_init) {
                /* will be set in gen_decl via init presence */
            }
            gen_decl(d);
        }

        if (accept(T_COMMA)) continue;
        break;
    }

    skip_attributes();
    parse_expect(T_SEMI, "expected ';' after declaration");
}

void parse_translation_unit(lexer_t *lx) {
    parser_set_lexer(lx);
    while (lx->cur.kind != T_EOF) {
        parse_top_level();
    }
}
