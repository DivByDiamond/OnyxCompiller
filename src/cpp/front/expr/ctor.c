/*
 * expr/ctor.c — functional casts and local ctor declarations (C).
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

static int64_t temp_slot(uint64_t bytes) {
    g_func.cur_offset += (int)bytes;
    g_func.cur_offset = (g_func.cur_offset + 7) & ~7;
    int64_t off = -(int64_t)g_func.cur_offset;
    if (g_func.cur_offset > g_func.frame_size)
        g_func.frame_size = g_func.cur_offset;
    return off;
}

/* `ClassName(args)` — construct a temporary in the frame and yield it as
 * an lvalue (used by `throw MyErr(7)` and copy-initialisation). */
bool cpp_functional_cast(lexer_t *lx, type_t *ct, val_t *out) {
    lex_next(lx);                       /* class name */
    uint64_t sz = type_sizeof(ct);
    if (sz == 0) sz = 1;
    int64_t off = temp_slot(sz);
    int ci = cpp_class_by_tag(ct->tag);
    val_t th;
    memset(&th, 0, sizeof(th));
    th.kind = VAL_REG;
    th.reg = RV_T0;
    th.type = type_make_ptr(ct);
    rv_addi_imm(RV_T0, RV_FP, (int)off);
    spill_ireg(&th);
    if (ci >= 0) {
        int nargs = cpp_count_call_args(lx);
        int gid = cpp_group_find(ci, ct->tag);
        int mi = (gid >= 0 && nargs >= 0)
               ? cpp_method_resolve_gid(gid, nargs) : -1;
        if (mi >= 0) {
            int sym = symtab_lookup_global(g_methods[mi].mangled);
            if (sym >= 0)
                gen_call(lx, sym, g_methods[mi].ftype, NULL, &th);
            else {
                cc_error("constructor '%s' has no definition", ct->tag);
                cpp_consume_parens(lx);
            }
        } else if (nargs > 0) {
            cc_error("no constructor for '%s' taking %d argument(s)",
                     ct->tag, nargs);
            cpp_consume_parens(lx);
        } else {
            cpp_consume_parens(lx);
        }
    } else {
        cpp_consume_parens(lx);
    }
    memset(out, 0, sizeof(*out));
    out->kind = VAL_LVAL;
    out->reg = RV_FP;
    out->offset = off;
    out->type = ct;
    return true;
}

/* Qualified name used as a primary: `Ns::Name` / `A::B::name`. Namespaces
 * are flattened (Phase D), so only the final component is resolved. */
bool cpp_is_ctor_call_site(lexer_t *lx, type_t *t) {
    (void)lx;
    if (!t || !t->tag[0]) return false;
    int ci = cpp_class_by_tag(t->tag);
    if (ci < 0) return false;
    return cpp_group_find(ci, g_classes[ci].tag) >= 0;
}

bool cpp_local_ctor_decl(lexer_t *lx, type_t *t) {
    int ci = cpp_class_by_tag(t->tag);
    if (ci < 0) return false;
    char name[CC_MAX_IDENT];
    strncpy(name, lx->cur.text, CC_MAX_IDENT - 1);
    name[CC_MAX_IDENT - 1] = 0;
    lex_next(lx);                       /* declarator name */
    int nargs = cpp_count_call_args(lx);
    if (nargs < 0) return false;
    int gid = cpp_group_find(ci, g_classes[ci].tag);
    int mi = cpp_method_resolve_gid(gid, nargs);
    if (mi < 0) {
        cc_error("no constructor for '%s' taking %d argument(s)",
                 t->tag, nargs);
        cpp_consume_parens(lx);
        return true;
    }
    int idx = symtab_lookup_global(g_methods[mi].mangled);
    if (idx < 0) {
        cc_error("constructor '%s' has no definition", t->tag);
        cpp_consume_parens(lx);
        return true;
    }

    /* Stack slot for the object (mirrors the C local path). */
    uint64_t sz = type_sizeof(t);
    if (sz == 0) sz = 1;
    uint64_t al = type_alignof(t);
    if (al < 1) al = 1;
    g_func.cur_offset += sz;
    g_func.cur_offset = (g_func.cur_offset + (int)al - 1) & ~((int)al - 1);
    int64_t off = -(int64_t)g_func.cur_offset;
    if (g_func.cur_offset > g_func.frame_size)
        g_func.frame_size = g_func.cur_offset;
    int lidx = symtab_install_local(name, SYM_LOCAL_VAR, t,
                                    g_cur_scope > 0 ? g_cur_scope : 1);
    g_locals[lidx].offset = off;

    /* Hidden this = &object. */
    val_t obj = cpp_local_val(lidx);
    materialize(&obj, RV_T0);           /* address in T0 */
    val_t th;
    memset(&th, 0, sizeof(th));
    th.kind = VAL_REG;
    th.reg = RV_T0;
    th.type = type_make_ptr(t);
    spill_ireg(&th);
    gen_call(lx, idx, g_methods[mi].ftype, NULL, &th);
    cpp_register_dtor(lidx, t);
    return true;
}
