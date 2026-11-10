/*
 * hooks.c — C++ lifecycle hooks (Phase C): destructor registration,
 * scope-exit and return unwinding, plus the exception stub (Phase E).
 *
 * Objects with a declared destructor are recorded per scope while the
 * body is compiled; cpp_scope_exit() emits their dtors (reverse order)
 * before the scope is popped, and cpp_return_unwind() emits every
 * still-live dtor before the epilogue. a0/fa0 are preserved across the
 * calls because the return value is already staged there.
 */
#include "core/compat.h"

#include "core/cc.h"
#include "core/types.h"
#include "front/lexer.h"
#include "front/ast.h"
#include "back/gen.h"
#include "arch/riscv64.h"
#include "cpp/cpp_front.h"
#include "cpp/cpp.h"

#define CPP_MAX_DTORS 64

typedef struct {
    int scope;
    int sym_idx;        /* local symtab index of the object */
    int dtor_sym;       /* global symbol of the mangled destructor */
    type_t *type;
} cpp_dtor_entry_t;

static cpp_dtor_entry_t g_dtors[CPP_MAX_DTORS];
static int g_n_dtors;

/* Mangled symbol of `Class::~Class()` (arity 0), or -1 if none. */
static int dtor_symbol_for(type_t *t) {
    if (!t || t->kind != TY_STRUCT || !t->tag[0]) return -1;
    int ci = cpp_class_by_tag(t->tag);
    if (ci < 0) return -1;
    char dname[CC_MAX_IDENT + 1];
    snprintf(dname, sizeof(dname), "~%s", t->tag);
    int gid = cpp_group_find(ci, dname);
    if (gid < 0) return -1;
    int mi = cpp_method_resolve_gid(gid, 0);
    if (mi < 0) return -1;
    return symtab_lookup_global(g_methods[mi].mangled);
}

/* Register a constructed local for destruction at scope end. */
void cpp_register_dtor(int sym_idx, type_t *t) {
    int dsym = dtor_symbol_for(t);
    if (dsym < 0) return;
    if (g_n_dtors >= CPP_MAX_DTORS)
        cc_fatal("too many C++ objects with destructors in one function");
    cpp_dtor_entry_t *d = &g_dtors[g_n_dtors++];
    d->scope = g_cur_scope;
    d->sym_idx = sym_idx;
    d->dtor_sym = dsym;
    d->type = t;
}

/* Emit `obj.~Class()` with `this` = &obj. */
static void emit_dtor(const cpp_dtor_entry_t *d) {
    val_t obj;
    memset(&obj, 0, sizeof(obj));
    obj.kind = VAL_SYM;
    obj.sym_idx = g_n_globals + d->sym_idx;
    obj.type = d->type;
    materialize(&obj, RV_T0);           /* object address */
    val_t th;
    memset(&th, 0, sizeof(th));
    th.kind = VAL_REG;
    th.reg = RV_T0;
    th.type = type_make_ptr(d->type);
    gen_call0(d->dtor_sym, &th);
}

/* Phase C: emit dtors of objects leaving the current scope. */
void cpp_scope_exit(lexer_t *lx) {
    (void)lx;
    for (int i = g_n_dtors - 1; i >= 0; i--)
        if (g_dtors[i].scope == g_cur_scope) emit_dtor(&g_dtors[i]);
    /* Drop the entries whose dtors just ran. */
    int keep = 0;
    for (int i = 0; i < g_n_dtors; i++)
        if (g_dtors[i].scope != g_cur_scope) g_dtors[keep++] = g_dtors[i];
    g_n_dtors = keep;
}

/* Phase C: emit dtors of all live locals before the epilogue. The return
 * value already sits in a0/fa0 — spill both across the calls. */
void cpp_return_unwind(lexer_t *lx) {
    (void)lx;
    cpp_eh_leave(lx);                   /* drop handlers opened in try */
    if (g_n_dtors == 0) return;
    g_func.cur_offset += 32;
    g_func.cur_offset = (g_func.cur_offset + 15) & ~15;
    int64_t slot = -(int64_t)g_func.cur_offset;
    if (g_func.cur_offset > g_func.frame_size)
        g_func.frame_size = g_func.cur_offset;
    rv_sd(RV_A0, RV_FP, (int)slot);
    rv_fsd(RV_FA0, RV_FP, (int)(slot + 8));
    for (int i = g_n_dtors - 1; i >= 0; i--) emit_dtor(&g_dtors[i]);
    rv_ld(RV_A0, RV_FP, (int)slot);
    rv_fld(RV_FA0, RV_FP, (int)(slot + 8));
}

/* Phase C: reset the per-function dtor registry. */
void cpp_func_begin(void) {
    g_n_dtors = 0;
    cpp_eh_enter();
}

/* Class-typed local: register the dtor, then run the default constructor
 * (arity 0) when one exists. */
void cpp_local_decl(lexer_t *lx, type_t *t, int sym_idx, bool had_init) {
    (void)lx; (void)had_init;
    if (!t || t->kind != TY_STRUCT || !t->tag[0]) return;
    if (sym_idx < 0 || sym_idx >= g_n_locals) return;
    cpp_register_dtor(sym_idx, t);
    int ci = cpp_class_by_tag(t->tag);
    if (ci < 0) return;
    cpp_class_t *c = &g_classes[ci];
    int gid = cpp_group_find(ci, c->tag);
    int mi = gid >= 0 ? cpp_method_resolve_gid(gid, 0) : -1;
    int idx = mi >= 0 ? symtab_lookup_global(g_methods[mi].mangled) : -1;
    if (idx < 0) {
        /* No user constructor: still initialise the vptr when the class
         * is polymorphic, otherwise virtual calls would read garbage. */
        if (c->polymorphic && c->vt_sym >= 0) {
            val_t obj;
            memset(&obj, 0, sizeof(obj));
            obj.kind = VAL_SYM;
            obj.sym_idx = g_n_globals + sym_idx;
            obj.type = t;
            materialize(&obj, RV_T0);
            gen_load_symbol_addr(RV_T1, c->vt_sym);
            rv_sd(RV_T1, RV_T0, 0);
        }
        return;
    }
    /* Hidden this = &local (the C decl path already reserved the slot). */
    val_t obj;
    memset(&obj, 0, sizeof(obj));
    obj.kind = VAL_SYM;
    obj.sym_idx = g_n_globals + sym_idx;
    obj.type = t;
    materialize(&obj, RV_T0);
    val_t th;
    memset(&th, 0, sizeof(th));
    th.kind = VAL_REG;
    th.reg = RV_T0;
    th.type = type_make_ptr(t);
    spill_ireg(&th);
    gen_call0(idx, &th);
}

/* Phase E: try/catch/throw statements (incl. trailing ';'). */
bool cpp_try_stmt(lexer_t *lx) {
    if (lx->cur.kind == T_KW_USING) {
        /* `using namespace N;` / `using N::x;` — namespaces are flattened,
         * so the directive is accepted and ignored (Phase D). */
        while (lx->cur.kind != T_SEMI && lx->cur.kind != T_EOF)
            lex_next(lx);
        if (lx->cur.kind == T_SEMI) lex_next(lx);
        return true;
    }
    if (lx->cur.kind == T_KW_TRY || lx->cur.kind == T_KW_THROW)
        return cpp_parse_exception_stmt(lx);
    return false;
}

/* ── Constructor vptr store (Phase D) ───────────────────────────────── */

static int g_ctor_ci = -1;

void cpp_set_ctor_class(int ci) { g_ctor_ci = ci; }
int cpp_ctor_class(void) { return g_ctor_ci; }

void cpp_method_prologue(lexer_t *lx) {
    (void)lx;
    int ci = g_ctor_ci;
    if (ci < 0) return;
    cpp_class_t *c = &g_classes[ci];
    if (!c->polymorphic || c->vt_sym < 0) return;
    int lidx = symtab_lookup_local("this");
    if (lidx < 0) return;
    rv_ld(RV_T0, RV_FP, (int)g_locals[lidx].offset);   /* this */
    gen_load_symbol_addr(RV_T1, c->vt_sym);
    rv_sd(RV_T1, RV_T0, 0);                            /* this->__vptr */
}
