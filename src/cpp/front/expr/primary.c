/*
 * expr/primary.c — primary hook: qualified names, refs, this, nullptr.
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

static bool qualified_primary(lexer_t *lx, val_t *out) {
    for (;;) {
        if (lx->cur.kind != T_IDENT) {
            cc_error("expected a name after '::'");
            out->kind = VAL_IMM;
            out->type = &ty_int;
            return true;
        }
        char name[CC_MAX_IDENT];
        strncpy(name, lx->cur.text, CC_MAX_IDENT - 1);
        name[CC_MAX_IDENT - 1] = 0;
        lex_peek(lx);
        int final = (lx->next.kind != T_SCOPE);
        lex_next(lx);
        if (!final) {
            lex_next(lx);                       /* '::' */
            continue;
        }
        int gidx = gen_lookup_global(name);
        if (gidx < 0) {
            cc_error("undeclared identifier '%s'", name);
            out->kind = VAL_IMM;
            out->type = &ty_int;
            return true;
        }
        memset(out, 0, sizeof(*out));
        out->kind = VAL_SYM;
        out->sym_idx = gidx;
        out->type = g_globals[gidx].type;
        return true;
    }
}

bool cpp_try_primary(lexer_t *lx, val_t *out) {
    if (lx->cur.kind == T_IDENT) {
        lex_peek(lx);
        if (lx->next.kind == T_SCOPE) return qualified_primary(lx, out);
        if (lx->next.kind == T_LT && cpp_template_find(lx->cur.text) >= 0)
            return cpp_template_instantiate(lx, lx->cur.text, out);
        type_t *ct = cpp_type_by_name(lx->cur.text);
        if (ct && lx->next.kind == T_LPAREN)
            return cpp_functional_cast(lx, ct, out);
    }
    if (lx->cur.kind == T_KW_NULLPTR) {
        lex_next(lx);
        out->kind = VAL_IMM;
        out->imm = 0;
        out->type = type_make_ptr(&ty_void);
        return true;
    }
    if (lx->cur.kind == T_KW_THIS) {
        lex_next(lx);
        int lidx = symtab_lookup_local("this");
        if (lidx < 0) {
            cc_error("'this' used outside a member function");
            out->kind = VAL_IMM;
            return true;
        }
        val_t th = cpp_local_val(lidx);
        load_value(&th, RV_T0);
        out->kind = VAL_REG;
        out->reg = RV_T0;
        out->type = g_locals[lidx].type;
        return true;
    }
    if (lx->cur.kind == T_KW_NEW) return cpp_parse_new(lx, out);
    if (lx->cur.kind == T_KW_DELETE) return cpp_parse_delete(lx, out);
    if (lx->cur.kind != T_IDENT) return false;
    /* C++ reference local: use site sees the REFERENCED object (auto
     * dereference), so it is an lvalue at the stored address. */
    {
        int lidx = symtab_lookup_local(lx->cur.text);
        if (lidx >= 0 && g_locals[lidx].type && g_locals[lidx].type->is_ref) {
            lex_next(lx);
            val_t pv = cpp_local_val(lidx);
            load_value(&pv, RV_T0);     /* stored pointer value */
            out->kind = VAL_LVAL;
            out->reg = RV_T0;
            out->offset = 0;
            out->type = g_locals[lidx].type->base;
            return true;
        }
    }
    if (symtab_lookup_local(lx->cur.text) >= 0) return false;
    if (gen_lookup_global(lx->cur.text) >= 0) return false;
    return cpp_bare_member(lx, out);
}

/* ── Member hook ────────────────────────────────────────────────────── */

