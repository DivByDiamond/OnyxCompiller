/*
 * decl/class/class.c — class body parser and member dispatch (Phase B).
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
#include "front/decl.h"

void cpp_skip_decl(lexer_t *lx) {
    int depth = 0;
    while (lx->cur.kind != T_EOF) {
        if (lx->cur.kind == T_LBRACE || lx->cur.kind == T_LPAREN ||
            lx->cur.kind == T_LBRACKET) {
            depth++;
            lex_next(lx);
            continue;
        }
        if (lx->cur.kind == T_RBRACE || lx->cur.kind == T_RPAREN ||
            lx->cur.kind == T_RBRACKET) {
            if (depth == 0 && lx->cur.kind == T_RBRACE) return; /* enclosing } */
            if (depth > 0) depth--;
            lex_next(lx);
            continue;
        }
        if (depth == 0 && lx->cur.kind == T_SEMI) {
            lex_next(lx);
            return;
        }
        lex_next(lx);
    }
}

static bool parse_member(lexer_t *lx, int ci, int access) {
    bool is_virtual = false, is_static = false;
    for (;;) {
        if (accept(T_KW_VIRTUAL)) { is_virtual = true; continue; }
        if (accept(T_KW_STATIC)) { is_static = true; continue; }
        if (accept(T_KW_EXPLICIT)) continue;         /* ctors only: ignore */
        if (accept(T_KW_INLINE)) continue;
        if (accept(T_KW_CONSTEXPR)) continue;
        if (lx->cur.kind == T_KW_FRIEND) {
            cc_error("friend declarations are not supported");
            lex_next(lx);
            cpp_skip_decl(lx);
            return true;
        }
        break;
    }

    cpp_class_t *c = &g_classes[ci];

    if (lx->cur.kind == T_TILDE)
        return cpp_parse_dtor(lx, ci, access);

    /* Constructor: the member name equals the class tag and is directly
     * followed by '(' (a method can never have the class name as its
     * return-less form). */
    if (lx->cur.kind == T_IDENT && strcmp(lx->cur.text, c->tag) == 0) {
        lex_peek(lx);
        if (lx->next.kind == T_LPAREN)
            return cpp_parse_ctor(lx, ci, access);
    }

    type_t *base = NULL;
    bool s = false, e = false, t = false, i = false;
    char ftag[CC_MAX_IDENT] = {0};
    if (!parse_decl_spec(lx, &base, &s, &e, &t, &i, ftag)) {
        parse_error("expected member declaration");
        return false;
    }
    (void)s; (void)e; (void)t; (void)i;
    if (lx->cur.kind == T_SEMI) {       /* stray ';' inside the body */
        lex_next(lx);
        return true;
    }

    char name[CC_MAX_IDENT] = {0};
    type_t *mt = parse_declarator(lx, base, name, sizeof(name));
    if (mt->kind == TY_FUNC)
        return cpp_parse_method(lx, ci, name, mt, access, is_virtual, is_static);

    if (lx->cur.kind == T_COLON) {
        cc_error("bitfields in classes are not supported");
        cpp_skip_decl(lx);
        return true;
    }
    if (lx->cur.kind == T_ASSIGN) {
        cc_error("default member initializers are not supported");
        cpp_skip_decl(lx);
        return true;
    }
    for (;;) {
        if (!name[0]) {
            parse_error("expected member name");
            return false;
        }
        cpp_class_append_field(ci, mt, name, access);
        if (!accept(T_COMMA)) break;
        mt = parse_declarator(lx, base, name, sizeof(name));
        if (mt->kind == TY_FUNC) {
            cc_error("function type in a member declarator list");
            break;
        }
    }
    parse_expect(T_SEMI, "expected ';' after member declaration");
    return true;
}

/* ── Class body ─────────────────────────────────────────────────────── */

void cpp_parse_class_body(lexer_t *lx, bool is_struct) {
    lex_next(lx);                       /* class / struct */
    if (lx->cur.kind != T_IDENT) {
        parse_error("expected class name");
        return;
    }
    char tag[CC_MAX_IDENT];
    strncpy(tag, lx->cur.text, CC_MAX_IDENT - 1);
    tag[CC_MAX_IDENT - 1] = 0;
    lex_next(lx);

    if (lx->cur.kind == T_SEMI) {       /* forward declaration */
        if (cpp_class_by_tag(tag) < 0)
            cpp_class_open(tag, is_struct ? CPP_ACC_PUBLIC : CPP_ACC_PRIVATE);
        lex_next(lx);
        return;
    }

    int ci = cpp_class_open(tag, is_struct ? CPP_ACC_PUBLIC : CPP_ACC_PRIVATE);
    if (ci < 0) return;
    if (accept(T_COLON)) {
        /* Base clause: [virtual] [public|private|protected] Name */
        for (;;) {
            if (lx->cur.kind == T_KW_VIRTUAL) {
                cc_error("virtual inheritance is not supported");
                lex_next(lx);
            }
            if (lx->cur.kind == T_KW_PUBLIC || lx->cur.kind == T_KW_PRIVATE ||
                lx->cur.kind == T_KW_PROTECTED) {
                lex_next(lx);
            }
            if (lx->cur.kind != T_IDENT) {
                parse_error("expected base class name");
                return;
            }
            char btag[CC_MAX_IDENT];
            strncpy(btag, lx->cur.text, CC_MAX_IDENT - 1);
            btag[CC_MAX_IDENT - 1] = 0;
            lex_next(lx);
            cpp_class_set_base(ci, btag);
            if (!accept(T_COMMA)) break;
        }
    }
    if (lx->cur.kind != T_LBRACE) {
        parse_error("expected '{' after class header");
        cpp_skip_decl(lx);
        return;
    }
    lex_next(lx);

    int access = g_classes[ci].default_access;
    while (lx->cur.kind != T_RBRACE && lx->cur.kind != T_EOF) {
        if (lx->cur.kind == T_KW_PUBLIC || lx->cur.kind == T_KW_PRIVATE ||
            lx->cur.kind == T_KW_PROTECTED) {
            access = (lx->cur.kind == T_KW_PUBLIC)   ? CPP_ACC_PUBLIC
                   : (lx->cur.kind == T_KW_PRIVATE)  ? CPP_ACC_PRIVATE
                                                     : CPP_ACC_PROTECTED;
            lex_next(lx);
            parse_expect(T_COLON, "expected ':' after access specifier");
            continue;
        }
        if (!parse_member(lx, ci, access)) {
            cpp_skip_decl(lx);
        }
    }
    parse_expect(T_RBRACE, "expected '}' after class body");
    if (lx->cur.kind == T_SEMI) {
        lex_next(lx);
    } else {
        cc_error("expected ';' after class definition");
        cpp_skip_decl(lx);
    }
    cpp_class_finish(ci);
}

/* ── Top-level seam ─────────────────────────────────────────────────── */

/* True when the current `struct` starts a definition (`struct X {`) or a
 * forward declaration (`struct X ;`) — those belong to the class parser.
 * `struct X *fn(...)` and `struct X v;` stay on the C path: routing them
 * here would eat the declarator after the tag. */
