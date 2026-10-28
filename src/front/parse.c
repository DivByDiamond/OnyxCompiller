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
#include "front/decl.h"

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
        /* Full declarator: handles pointers, arrays, function types and
         * function pointers via the splice algorithm in decl.c. */
        char name[CC_MAX_IDENT] = {0};
        cc_pos_t pos = g_lx->cur.pos;
        type_t *t = parse_declarator(g_lx, base, name, sizeof(name));

        if (name[0] == 0) {
            parse_error("expected identifier");
            if (g_lx->cur.kind != T_SEMI && g_lx->cur.kind != T_EOF) {
                lex_next(g_lx);
            }
            return;
        }

        if (t->kind == TY_FUNC) {
            /* Function definition or prototype. */
            skip_attributes();

            if (g_lx->cur.kind == T_LBRACE) {
                decl_t *d = ast_new_decl(D_FUNC_DEF, pos);
                d->type = t;
                strncpy(d->name, name, CC_MAX_IDENT - 1);
                d->is_static = is_static;
                if (is_inline) t->is_inline = true;
                gen_decl(d);
                return;
            } else {
                decl_t *d = ast_new_decl(D_FUNC_DECL, pos);
                d->type = t;
                strncpy(d->name, name, CC_MAX_IDENT - 1);
                d->is_static = is_static;
                if (is_inline) t->is_inline = true;
                gen_decl(d);
            }
        } else {
            expr_t *init = NULL;
            if (accept(T_ASSIGN)) {
                init = gen_parse_global_init(g_lx, t);
            }
            decl_t *d = ast_new_decl(D_VAR, pos);
            d->type = t;
            strncpy(d->name, name, CC_MAX_IDENT - 1);
            d->init = init;
            d->is_static = is_static;
            d->is_extern = is_extern;
            if (is_typedef) {
                d->kind = D_TYPEDEF;
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
        size_t before = lx->pos;      /* byte offset before parsing this decl */
        parse_top_level();
        /* Error recovery: if a malformed declaration consumed nothing, skip
         * one token to guarantee forward progress (prevents infinite error
         * loops that used to hang the compiler on bad input). */
        if (lx->pos == before && lx->cur.kind != T_EOF) {
            parse_error("skipping token to recover");
            lex_next(lx);
        }
    }
}
