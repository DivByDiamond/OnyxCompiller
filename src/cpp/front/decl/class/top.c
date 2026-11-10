/*
 * decl/class/top.c — top-level C++ dispatch (class/ns/using/template).
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

static bool struct_is_definition(lexer_t *lx) {
    lexer_t t = *lx;                    /* value copy: safe to advance */
    lex_next(&t);                       /* 'struct' */
    if (t.cur.kind != T_IDENT) return false;
    lex_next(&t);                       /* tag */
    return t.cur.kind == T_LBRACE || t.cur.kind == T_SEMI;
}

bool cpp_top_level(lexer_t *lx) {
    if (lx->cur.kind == T_KW_CLASS) {
        cpp_parse_class_body(lx, false);
        return true;
    }
    if (lx->cur.kind == T_KW_STRUCT && struct_is_definition(lx)) {
        cpp_parse_class_body(lx, true);
        return true;
    }
    if (lx->cur.kind == T_KW_NAMESPACE) return cpp_parse_namespace(lx);
    if (lx->cur.kind == T_KW_USING) return cpp_parse_using(lx);
    if (lx->cur.kind == T_KW_TEMPLATE) return cpp_parse_template(lx);
    return false;
}
