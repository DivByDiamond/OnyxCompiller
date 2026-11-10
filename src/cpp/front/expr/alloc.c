/*
 * expr/alloc.c — `new` / `delete` lowering (Phase C).
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

static int ensure_proto(const char *name, type_t *ftype) {
    int idx = symtab_lookup_global(name);
    if (idx >= 0) return idx;
    return symtab_install_global(name, SYM_FUNCTION, ftype);
}

/* `new T [ (args) ]` — malloc + constructor. */
bool cpp_parse_new(lexer_t *lx, val_t *out) {
    lex_next(lx);                       /* new */
    if (!is_type_start(lx)) {
        cc_error("expected a type after 'new'");
        out->kind = VAL_IMM;
        out->type = type_make_ptr(&ty_void);
        return true;
    }
    type_t *t = parse_type_spec(lx);
    if (lx->cur.kind == T_LBRACKET) {
        cc_error("array new/delete is not supported");
        while (lx->cur.kind != T_SEMI && lx->cur.kind != T_EOF &&
               lx->cur.kind != T_RPAREN)
            lex_next(lx);
        out->kind = VAL_IMM;
        out->type = type_make_ptr(t);
        return true;
    }
    uint64_t sz = type_sizeof(t);
    if (sz == 0) sz = 1;
    type_t *ptr = type_make_ptr(t);
    type_t *mft = type_make_function(ptr);
    type_ensure_params(mft);
    mft->params[0].type = &ty_ulong;
    mft->nparams = 1;
    int midx = ensure_proto("malloc", mft);
    val_t szv;
    memset(&szv, 0, sizeof(szv));
    szv.kind = VAL_IMM;
    szv.imm = (int64_t)sz;
    szv.type = &ty_ulong;
    val_t mem = gen_call1(midx, mft, &szv);      /* pointer in a0 */
    mem.type = ptr;

    int ci = t->tag[0] ? cpp_class_by_tag(t->tag) : -1;
    if (ci >= 0) {
        int gid = cpp_group_find(ci, g_classes[ci].tag);
        if (gid < 0 && g_classes[ci].polymorphic && g_classes[ci].vt_sym >= 0) {
            /* No constructor but polymorphic: set the vptr explicitly. */
            spill_ireg(&mem);
            val_t m1 = mem;             /* copy: load_value mutates it */
            load_value(&m1, RV_T0);
            gen_load_symbol_addr(RV_T1, g_classes[ci].vt_sym);
            rv_sd(RV_T1, RV_T0, 0);
            load_value(&mem, RV_T0);    /* mem is still the spilled pointer */
            out->kind = VAL_REG;
            out->reg = RV_T0;
            out->type = ptr;
            return true;
        }
        if (gid >= 0) {
            spill_ireg(&mem);           /* keep the pointer across the ctor */
            if (lx->cur.kind == T_LPAREN) {
                int nargs = cpp_count_call_args(lx);
                int mi = cpp_method_resolve_gid(gid, nargs);
                if (mi < 0) {
                    cc_error("no constructor for 'new %s' taking %d argument(s)",
                             t->tag, nargs);
                    cpp_consume_parens(lx);
                } else {
                    int sym = symtab_lookup_global(g_methods[mi].mangled);
                    if (sym >= 0) {
                        val_t th = mem;     /* copy: gen_call mutates it */
                        gen_call(lx, sym, g_methods[mi].ftype, NULL, &th);
                    }
                }
            } else {
                int mi = cpp_method_resolve_gid(gid, 0);
                if (mi >= 0) {
                    int sym = symtab_lookup_global(g_methods[mi].mangled);
                    if (sym >= 0) {
                        val_t th = mem;     /* copy: gen_call0 mutates it */
                        gen_call0(sym, &th);
                    }
                }
            }
            load_value(&mem, RV_T0);    /* a0 held the ctor's result */
            out->kind = VAL_REG;
            out->reg = RV_T0;
        } else {
            *out = mem;
        }
    } else {
        *out = mem;
    }
    out->type = ptr;
    return true;
}

/* `delete p` — destructor (when the pointee is a class) + free. */
bool cpp_parse_delete(lexer_t *lx, val_t *out) {
    lex_next(lx);                       /* delete */
    if (lx->cur.kind == T_LBRACKET) {
        cc_error("array new/delete is not supported");
        while (lx->cur.kind != T_SEMI && lx->cur.kind != T_EOF &&
               lx->cur.kind != T_RPAREN)
            lex_next(lx);
        out->kind = VAL_IMM;
        out->type = &ty_void;
        return true;
    }
    val_t p = parse_expr(lx);
    spill_ireg(&p);                     /* pointer reused by dtor and free */
    type_t *obj = (p.type && p.type->kind == TY_PTR) ? p.type->base : p.type;
    int ci = (obj && obj->tag[0]) ? cpp_class_by_tag(obj->tag) : -1;
    if (ci >= 0) {
        char dname[CC_MAX_IDENT + 1];
        snprintf(dname, sizeof(dname), "~%s", obj->tag);
        int gid = cpp_group_find(ci, dname);
        int mi = gid >= 0 ? cpp_method_resolve_gid(gid, 0) : -1;
        if (mi >= 0) {
            int sym = symtab_lookup_global(g_methods[mi].mangled);
            if (sym >= 0) {
                val_t th = p;           /* copy: gen_call0 mutates it */
                gen_call0(sym, &th);
            }
        }
    }
    type_t *fft = type_make_function(&ty_void);
    type_ensure_params(fft);
    fft->params[0].type = type_make_ptr(&ty_void);
    fft->nparams = 1;
    int fidx = ensure_proto("free", fft);
    val_t farg = p;                     /* copy: gen_call1 mutates it */
    gen_call1(fidx, fft, &farg);
    out->kind = VAL_IMM;
    out->type = &ty_void;
    out->imm = 0;
    return true;
}

/* ── Primary hook ───────────────────────────────────────────────────── */

/* Bare member name inside a method body: `x` -> `this->x`. */
