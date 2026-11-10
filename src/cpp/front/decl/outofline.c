/*
 * decl/outofline.c — out-of-line member definitions (Phase B).
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
#include "cpp/mangle.h"

static bool out_of_line_ctor_dtor(lexer_t *lx, int ci) {
    if (lx->cur.kind == T_TILDE)
        return cpp_parse_dtor(lx, ci, CPP_ACC_PUBLIC);
    if (lx->cur.kind == T_IDENT && strcmp(lx->cur.text, g_classes[ci].tag) == 0)
        return cpp_parse_ctor(lx, ci, cpp_method_access(ci, g_classes[ci].tag, -1));
    parse_error("expected constructor or destructor name");
    cpp_skip_decl(lx);
    return true;
}

/* True when `name(arity)` overrides a virtual declared in the chain. */
static bool out_of_line_member(lexer_t *lx, type_t *base) {
    char cls[CC_MAX_IDENT];
    strncpy(cls, lx->cur.text, CC_MAX_IDENT - 1);
    cls[CC_MAX_IDENT - 1] = 0;
    lex_next(lx);                       /* class name */
    lex_next(lx);                       /* :: */
    int ci = cpp_class_by_tag(cls);
    if (ci < 0) {
        cc_error("'%s' is not a class", cls);
        cpp_skip_decl(lx);
        return true;
    }
    if (lx->cur.kind == T_TILDE)
        return cpp_parse_dtor(lx, ci, CPP_ACC_PUBLIC);

    if (lx->cur.kind != T_IDENT) {
        parse_error("expected member name after '::'");
        cpp_skip_decl(lx);
        return true;
    }
    char name[CC_MAX_IDENT];
    strncpy(name, lx->cur.text, CC_MAX_IDENT - 1);
    name[CC_MAX_IDENT - 1] = 0;
    lex_next(lx);

    type_t *ft = type_make_function(base);
    parse_expect(T_LPAREN, "expected '(' in member definition");
    parse_param_list(lx, ft);
    parse_expect(T_RPAREN, "expected ')'");
    while (lx->cur.kind == T_KW_CONST || lx->cur.kind == T_KW_VOLATILE)
        lex_next(lx);

    int arity = ft->nparams;
    char mangled[CC_MAX_IDENT];
    if (!cpp_mangle_member(mangled, sizeof(mangled), g_classes[ci].tag, name,
                           ft, arity)) {
        cpp_skip_decl(lx);
        return true;
    }
    cpp_insert_this(ft, g_classes[ci].type, false);
    int access = cpp_method_access(ci, name, arity);
    cpp_method_add(ci, name, arity, access, ft, mangled, true,
                   cpp_override_is_virtual(ci, name, arity));
    if (lx->cur.kind != T_LBRACE) {
        cc_error("out-of-line member '%s' must be a definition", name);
        cpp_skip_decl(lx);
        return true;
    }
    cpp_compile_method(lx, ci, mangled, ft);
    return true;
}

bool cpp_try_out_of_line(lexer_t *lx, type_t *base) {
    if (lx->cur.kind == T_SCOPE) {
        /* `Foo::Foo(...)` / `Foo::~Foo()`: the "type" was the class name. */
        lex_next(lx);
        int ci = cpp_class_by_tag(base->tag);
        if (ci < 0) {
            cc_error("'%s' is not a class", base->tag);
            cpp_skip_decl(lx);
            return true;
        }
        return out_of_line_ctor_dtor(lx, ci);
    }
    if (lx->cur.kind != T_IDENT) return false;
    lex_peek(lx);
    if (lx->next.kind != T_SCOPE) return false;
    return out_of_line_member(lx, base);
}
