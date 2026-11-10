/*
 * decl/method.c — member declarations/definitions and mangling glue.
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

void cpp_insert_this(type_t *ft, type_t *cls, bool is_static) {
    if (is_static) return;
    type_ensure_params(ft);
    if (ft->nparams >= CC_MAX_FUNC_PARAMS) {
        cc_error("too many parameters");
        return;
    }
    for (int i = ft->nparams; i > 0; i--)
        ft->params[i] = ft->params[i - 1];
    strncpy(ft->params[0].name, "this", CC_MAX_IDENT - 1);
    ft->params[0].name[CC_MAX_IDENT - 1] = 0;
    ft->params[0].type = type_make_ptr(cls);
    ft->params[0].is_used = true;
    ft->nparams++;
}

/* Compile a member body through gen_decl with the class context set. */
void cpp_compile_method(lexer_t *lx, int ci, const char *mangled,
                           type_t *ft) {
    decl_t *d = ast_new_decl(D_FUNC_DEF, lx->cur.pos);
    d->type = ft;
    strncpy(d->name, mangled, CC_MAX_IDENT - 1);
    d->name[CC_MAX_IDENT - 1] = 0;
    int prev = cpp_method_class();
    cpp_set_method_class(ci);
    gen_decl(d);
    cpp_set_method_class(prev);
}

/* `= default` / `= delete` after a member signature. True when consumed. */
static bool parse_default_delete(lexer_t *lx) {
    if (lx->cur.kind != T_ASSIGN) return false;
    lex_next(lx);
    if (lx->cur.kind == T_KW_DEFAULT || lx->cur.kind == T_KW_DELETE) {
        lex_next(lx);
        parse_expect(T_SEMI, "expected ';' after '= default/delete'");
        return true;
    }
    cc_error("unsupported '= value' in a member declaration");
    cpp_skip_decl(lx);
    return true;
}

bool cpp_parse_ctor(lexer_t *lx, int ci, int access) {
    const char *tag = g_classes[ci].tag;
    lex_next(lx);                       /* class name */
    parse_expect(T_LPAREN, "expected '(' after constructor name");
    type_t *ft = type_make_function(&ty_void);
    parse_param_list(lx, ft);
    parse_expect(T_RPAREN, "expected ')'");
    if (lx->cur.kind == T_COLON) {
        cc_error("constructor initializer lists are not supported");
        cpp_skip_decl(lx);
        return true;
    }

    int arity = ft->nparams;
    char mangled[CC_MAX_IDENT];
    if (!cpp_mangle_member(mangled, sizeof(mangled), tag, tag, ft, arity)) {
        cpp_skip_decl(lx);
        return true;
    }
    cpp_insert_this(ft, g_classes[ci].type, false);
    bool defining = (lx->cur.kind == T_LBRACE);
    cpp_method_add(ci, tag, arity, access, ft, mangled, defining, false);
    if (parse_default_delete(lx)) return true;
    if (defining) {
        cpp_set_ctor_class(ci);        /* vptr store in the prologue */
        cpp_compile_method(lx, ci, mangled, ft);
        cpp_set_ctor_class(-1);
    }
    else
        parse_expect(T_SEMI, "expected ';' after constructor declaration");
    return true;
}

bool cpp_parse_dtor(lexer_t *lx, int ci, int access) {
    const char *tag = g_classes[ci].tag;
    lex_next(lx);                       /* '~' */
    if (lx->cur.kind != T_IDENT || strcmp(lx->cur.text, tag) != 0) {
        parse_error("expected destructor name");
        return false;
    }
    lex_next(lx);
    parse_expect(T_LPAREN, "expected '(' after destructor name");
    parse_expect(T_RPAREN, "destructors take no parameters");

    char dname[CC_MAX_IDENT];
    snprintf(dname, sizeof(dname), "~%s", tag);
    type_t *ft = type_make_function(&ty_void);
    char mangled[CC_MAX_IDENT];
    if (!cpp_mangle_member(mangled, sizeof(mangled), tag, dname, ft, 0)) {
        cpp_skip_decl(lx);
        return true;
    }
    cpp_insert_this(ft, g_classes[ci].type, false);
    bool defining = (lx->cur.kind == T_LBRACE);
    cpp_method_add(ci, dname, 0, access, ft, mangled, defining, false);
    if (parse_default_delete(lx)) return true;
    if (defining)
        cpp_compile_method(lx, ci, mangled, ft);
    else
        parse_expect(T_SEMI, "expected ';' after destructor declaration");
    return true;
}

bool cpp_parse_method(lexer_t *lx, int ci, const char *name, type_t *ft,
                      int access, bool is_virtual, bool is_static) {
    (void)is_virtual;                   /* vtable dispatch lands in Phase D */
    /* Trailing cv/override qualifiers are accepted and ignored. */
    while (lx->cur.kind == T_KW_CONST || lx->cur.kind == T_KW_VOLATILE)
        lex_next(lx);
    if (lx->cur.kind == T_KW_OVERRIDE) lex_next(lx);

    int arity = ft->nparams;
    char mangled[CC_MAX_IDENT];
    if (!cpp_mangle_member(mangled, sizeof(mangled), g_classes[ci].tag, name,
                           ft, arity)) {
        cpp_skip_decl(lx);
        return true;
    }
    cpp_insert_this(ft, g_classes[ci].type, is_static);

    /* A method that matches a base virtual by (name, arity) is virtually
     * dispatched even without repeating the `virtual` keyword. */
    if (!is_virtual)
        is_virtual = cpp_override_is_virtual(ci, name, arity);

    bool pure = false;
    if (lx->cur.kind == T_ASSIGN) {
        lex_next(lx);
        if (lx->cur.kind == T_INT && lx->cur.ival == 0) {
            pure = true;                /* = 0 -> pure virtual */
            lex_next(lx);
        } else {
            cc_error("unsupported '= value' in a method declaration");
            cpp_skip_decl(lx);
            return true;
        }
    }
    if (pure) g_classes[ci].is_abstract = true;

    bool defining = (lx->cur.kind == T_LBRACE);
    cpp_method_add(ci, name, arity, access, ft, mangled, defining, is_virtual);
    if (defining)
        cpp_compile_method(lx, ci, mangled, ft);
    else {
        /* Install the prototype so a vtable built at class finish can
         * reference a definition that appears later (out-of-line). */
        symtab_install_global(mangled, SYM_FUNCTION, ft);
        parse_expect(T_SEMI, "expected ';' after method declaration");
    }
    return true;
}

int cpp_method_access(int ci, const char *name, int arity) {
    int gid = cpp_group_find(ci, name);
    if (gid >= 0) {
        int mi = cpp_method_resolve_gid(gid, arity);
        if (mi >= 0) return g_methods[mi].access;
    }
    return CPP_ACC_PUBLIC;
}

/* ── Out-of-line definitions ────────────────────────────────────────── */

/* Continue after `Class::` for a constructor (`Name(...)`) or, when the
 * token is '~', a destructor. */
bool cpp_override_is_virtual(int ci, const char *name, int arity) {
    for (int c = ci; c >= 0; c = g_classes[c].base_idx)
        for (int i = 0; i < g_n_methods; i++)
            if (g_methods[i].is_virtual &&
                g_groups[g_methods[i].group].owner == c &&
                g_methods[i].arity == arity &&
                strcmp(g_groups[g_methods[i].group].name, name) == 0)
                return true;
    return false;
}

