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
#include "cpp/cpp.h"

lexer_t *g_lx = NULL;
bool g_cpp_mode = false;

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

/* Parse a top-level declaration. */
static void parse_top_level(void) {
    /* C++ seam: class/template/namespace/out-of-line `X::y` decls are
     * owned by the cpp module (Phases B/D/F); false in Phase A -> the
     * normal C declaration path below runs unchanged. */
    if (g_cpp_mode && cpp_top_level(g_lx)) return;

    bool prefix_weak = read_attributes();
    type_t *base = NULL;
    bool is_static = false, is_extern = false, is_typedef = false, is_inline = false;
    /* Attributes may surround storage specifiers, but do not split a
     * multi-token base type (e.g. unsigned long). Keep state on this stack:
     * parsing a body must never inherit the enclosing declaration's weak. */
    for (;;) {
        switch (g_lx->cur.kind) {
            case T_KW_STATIC: is_static = true; break;
            case T_KW_EXTERN: is_extern = true; break;
            case T_KW_TYPEDEF: is_typedef = true; break;
            case T_KW_INLINE: is_inline = true; break;
            default: goto storage_done;
        }
        lex_next(g_lx);
        if (read_attributes()) prefix_weak = true;
    }
storage_done:;
    bool spec_static = false, spec_extern = false;
    bool spec_typedef = false, spec_inline = false;
    char tag[CC_MAX_IDENT] = {0};
    if (!parse_decl_spec(g_lx, &base, &spec_static, &spec_extern, &spec_typedef, &spec_inline, tag)) {
        if (is_static || is_extern || is_typedef || is_inline) {
            base = &ty_int; /* Existing implicit-int storage-class grammar. */
        } else {
            if (prefix_weak) parse_error("weak requires a named declaration");
            if (accept(T_SEMI)) return;
            parse_error("expected declaration");
            lex_next(g_lx);
            return;
        }
    }

    is_static |= spec_static;
    is_extern |= spec_extern;
    is_typedef |= spec_typedef;
    is_inline |= spec_inline;
    if (read_attributes()) prefix_weak = true;
    if (prefix_weak && (is_static || is_typedef))
        parse_error("weak is not supported on static or typedef declarations");

    /* C++ seam: out-of-line member definitions (`int Foo::bar() {...}`,
     * `Foo::Foo(...)`, `Foo::~Foo()`) — the return type (or the class
     * name of a ctor) was just parsed. */
    if (g_cpp_mode && cpp_try_out_of_line(g_lx, base)) {
        if (prefix_weak)
            parse_error("weak is not supported on out-of-line members");
        return;
    }

    if (g_lx->cur.kind == T_SEMI) {
        if (prefix_weak) parse_error("weak requires a named declaration");
        lex_next(g_lx);
        return;
    }

    for (;;) {
        bool declarator_weak = read_attributes();
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

        bool is_weak = read_attributes() || declarator_weak || prefix_weak;
        if (is_weak && (is_static || is_typedef))
            parse_error("weak is not supported on static or typedef declarations");

        if (t->kind == TY_FUNC) {
            /* Function definition or prototype. */

            if (g_lx->cur.kind == T_LBRACE) {
                decl_t *d = ast_new_decl(D_FUNC_DEF, pos);
                d->type = t;
                strncpy(d->name, name, CC_MAX_IDENT - 1);
                d->is_static = is_static;
                d->is_weak = is_weak;
                if (is_inline) t->is_inline = true;
                gen_decl(d);
                return;
            } else {
                decl_t *d = ast_new_decl(D_FUNC_DECL, pos);
                d->type = t;
                strncpy(d->name, name, CC_MAX_IDENT - 1);
                d->is_static = is_static;
                d->is_weak = is_weak;
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
            d->is_weak = is_weak;
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
        /* C++: compile template bodies queued during this declaration
         * (at top level, so they never splice into an active function). */
        if (g_cpp_mode) cpp_drain_pending_templates();
        /* Error recovery: if a malformed declaration consumed nothing, skip
         * one token to guarantee forward progress (prevents infinite error
         * loops that used to hang the compiler on bad input). */
        if (lx->pos == before && lx->cur.kind != T_EOF) {
            parse_error("skipping token to recover");
            lex_next(lx);
        }
    }
}

/* Exposed to the C++ namespace parser (same translation unit, same
 * lexer): one top-level declaration with the normal dispatch. */
void parse_top_level_decl(void) {
    parse_top_level();
}
