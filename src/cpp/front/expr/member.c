/*
 * expr/member.c — method markers, hidden `this`, field access (Phase B).
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

/* Pending `this` saved by the marker hooks (see cpp/cpp.h). */
static val_t g_pending_this;
static bool g_pending_this_valid;

static void save_pending_this(val_t *v, int is_arrow) {
    val_t t;
    memset(&t, 0, sizeof(t));
    if (is_arrow) {
        load_value(v, RV_T0);           /* pointer value */
        t.kind = VAL_REG;
        t.reg = RV_T0;
        t.type = v->type;
    } else {
        materialize(v, RV_T0);          /* address of the object */
        t.kind = VAL_REG;
        t.reg = RV_T0;
        t.type = type_make_ptr(v->type);
    }
    spill_ireg(&t);
    g_pending_this = t;
    g_pending_this_valid = true;
}

/* Local symbol as a VAL_SYM (locals live above g_n_globals). */
val_t cpp_local_val(int lidx) {
    val_t v;
    memset(&v, 0, sizeof(v));
    v.kind = VAL_SYM;
    v.sym_idx = g_n_globals + lidx;
    v.type = g_locals[lidx].type;
    return v;
}

/* Token-level scan of the call arguments (no consumption). */
int cpp_count_call_args(lexer_t *lx) {
    if (lx->cur.kind != T_LPAREN) return -1;
    lexer_t t = *lx;                    /* value copy: safe to advance */
    lex_next(&t);                       /* past '(' */
    int depth = 1;
    int commas = 0;
    bool any = false;
    while (t.cur.kind != T_EOF) {
        if (t.cur.kind == T_RPAREN) {
            depth--;
            if (depth == 0) break;
        } else if (t.cur.kind == T_LPAREN) {
            depth++;
            any = true;
        } else if (t.cur.kind == T_COMMA) {
            if (depth == 1) commas++;
        } else {
            any = true;
        }
        lex_next(&t);
    }
    if (t.cur.kind == T_EOF) return -1;
    return any ? commas + 1 : 0;
}

void cpp_consume_parens(lexer_t *lx) {
    if (lx->cur.kind != T_LPAREN) return;
    int depth = 0;
    do {
        if (lx->cur.kind == T_LPAREN) depth++;
        else if (lx->cur.kind == T_RPAREN) depth--;
        lex_next(lx);
    } while (depth > 0 && lx->cur.kind != T_EOF);
}

/* Prototype for a runtime helper; grown lazily so a .cpp can call malloc
 * before the auto-linked libonyxc sources are parsed. */
bool cpp_bare_member(lexer_t *lx, val_t *out) {
    int ci = cpp_method_class();
    if (ci < 0) return false;
    int lidx = symtab_lookup_local("this");
    if (lidx < 0) return false;
    type_t *cls = g_classes[ci].type;
    int fi = cpp_class_field(ci, lx->cur.text);
    if (fi >= 0) {
        val_t th = cpp_local_val(lidx);
        load_value(&th, RV_T0);         /* object pointer */
        lex_next(lx);
        memset(out, 0, sizeof(*out));
        out->kind = VAL_LVAL;
        out->reg = RV_T0;
        out->offset = (int64_t)cls->fields[fi].offset;
        out->type = cls->fields[fi].type;
        return true;
    }
    int gid = cpp_group_find(ci, lx->cur.text);
    if (gid < 0) return false;
    lex_next(lx);
    val_t th = cpp_local_val(lidx);
    load_value(&th, RV_T0);
    val_t saved;
    memset(&saved, 0, sizeof(saved));
    saved.kind = VAL_REG;
    saved.reg = RV_T0;
    saved.type = g_locals[lidx].type;
    spill_ireg(&saved);
    g_pending_this = saved;
    g_pending_this_valid = true;
    memset(out, 0, sizeof(*out));
    out->kind = VAL_SYM;
    out->sym_idx = -1;
    out->cpp_group = gid + 1;
    out->type = &ty_void;
    return true;
}

/* Reserved frame slot (same rule as gen.c local allocation). */
bool cpp_try_member(lexer_t *lx, val_t *v, int is_arrow,
                    const char *mname, type_t *st) {
    (void)lx;
    if (!st || !st->tag[0]) return false;
    int ci = cpp_class_by_tag(st->tag);
    if (ci < 0) return false;
    int gid = cpp_group_find(ci, mname);
    if (gid < 0) return false;
    save_pending_this(v, is_arrow);
    memset(v, 0, sizeof(*v));           /* method marker */
    v->kind = VAL_SYM;
    v->sym_idx = -1;
    v->cpp_group = gid + 1;
    v->type = &ty_void;
    return true;
}

void cpp_check_field_access(lexer_t *lx, const char *mname, type_t *st) {
    cpp_check_member_access(lx, mname, st);
}

/* ── Call hook ──────────────────────────────────────────────────────── */

bool cpp_try_call(lexer_t *lx, val_t *v) {
    if (v->cpp_group <= 0) return false;
    int gid = v->cpp_group - 1;
    int nargs = cpp_count_call_args(lx);
    if (nargs < 0) return false;        /* not '(' */
    int mi = cpp_method_resolve_gid(gid, nargs);
    if (mi < 0) {
        cc_error("no method overload taking %d argument(s)", nargs);
        cpp_consume_parens(lx);
        memset(v, 0, sizeof(*v));
        v->kind = VAL_IMM;
        v->type = &ty_int;
        g_pending_this_valid = false;
        return true;
    }
    int owner = g_groups[gid].owner;
    if (!cpp_access_allows(cpp_method_class(), owner, g_methods[mi].access))
        cc_error("'%s' is %s in '%s'", g_groups[gid].name,
                 cpp_access_str(g_methods[mi].access), g_classes[owner].tag);
    /* Virtual dispatch: load the callee from the object's vtable. The
     * pending-this value is a spilled object pointer, so it survives the
     * argument evaluation inside gen_call. */
    if (g_methods[mi].is_virtual && g_pending_this_valid) {
        val_t th = g_pending_this;
        load_value(&th, RV_T0);                 /* object pointer */
        rv_ld(RV_T1, RV_T0, 0);                 /* vptr */
        rv_ld(RV_T2, RV_T1, g_methods[mi].slot * 8);
        val_t callee;
        memset(&callee, 0, sizeof(callee));
        callee.kind = VAL_REG;
        callee.reg = RV_T2;
        callee.type = type_make_ptr(g_methods[mi].ftype);
        spill_ireg(&callee);                    /* survives arg evaluation */
        val_t thisarg = g_pending_this;         /* copy (gen_call mutates) */
        g_pending_this_valid = false;
        *v = gen_call(lx, -1, g_methods[mi].ftype, &callee, &thisarg);
        return true;
    }
    int idx = symtab_lookup_global(g_methods[mi].mangled);
    if (idx < 0) {
        cc_error("method '%s' has no definition", g_groups[gid].name);
        cpp_consume_parens(lx);
        memset(v, 0, sizeof(*v));
        v->kind = VAL_IMM;
        v->type = &ty_int;
        g_pending_this_valid = false;
        return true;
    }
    val_t *hidden = g_pending_this_valid ? &g_pending_this : NULL;
    g_pending_this_valid = false;
    *v = gen_call(lx, idx, g_methods[mi].ftype, NULL, hidden);
    return true;
}

/* ── Local ctor declaration (`Foo x(1,2);`) ─────────────────────────── */

