/*
 * stmt/eh_rt.c — exception runtime glue: tags, frame slots, calls.
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

int g_eh_depth;

int cpp_tag_for_type(type_t *t) {
    if (!t) return 0;
    if (t->kind == TY_STRUCT && t->tag[0]) {
        unsigned h = 2166136261u;            /* FNV-1a */
        for (const char *p = t->tag; *p; p++) {
            h ^= (unsigned char)*p;
            h *= 16777619u;
        }
        h &= 0x7fffffffu;
        return h ? (int)h : 1;
    }
    if (t->kind == TY_INT) return 1;
    if (t->kind == TY_PTR) return 2;
    return 3;
}

/* Reserve a frame slot; returns its fp-relative offset. */
int64_t cpp_frameslot(uint64_t bytes, uint64_t align) {
    if (align < 1) align = 1;
    g_func.cur_offset += (int)bytes;
    g_func.cur_offset = (g_func.cur_offset + (int)align - 1) & ~((int)align - 1);
    int64_t off = -(int64_t)g_func.cur_offset;
    if (g_func.cur_offset > g_func.frame_size)
        g_func.frame_size = g_func.cur_offset;
    return off;
}

/* Prototype for a runtime helper (lazily installed; the definition is
 * auto-linked from libonyxc). */
int cpp_eh_sym(const char *name, type_t *ret, type_t *a0, type_t *a1) {
    int idx = symtab_lookup_global(name);
    if (idx >= 0) return idx;
    type_t *ft = type_make_function(ret);
    type_ensure_params(ft);
    if (a0) {
        memset(&ft->params[ft->nparams], 0, sizeof(ft->params[0]));
        ft->params[ft->nparams++].type = a0;
    }
    if (a1) {
        memset(&ft->params[ft->nparams], 0, sizeof(ft->params[0]));
        ft->params[ft->nparams++].type = a1;
    }
    return symtab_install_global(name, SYM_FUNCTION, ft);
}

void cpp_emit_call(int sym) {
    gen_load_symbol_addr(RV_T0, sym);
    rv_jalr(RV_RA, RV_T0, 0);
}

/* Frame-slot int argument in a0/a1 for the eh runtime helpers. */
void cpp_emit_call_slots(int sym, int64_t a0slot, int64_t a1slot) {
    rv_addi_imm(RV_A0, RV_FP, (int)a0slot);
    rv_addi_imm(RV_A1, RV_FP, (int)a1slot);
    cpp_emit_call(sym);
}

/* ---- return-time handler unwinding ---------------------------------- */

void cpp_eh_enter(void) { g_eh_depth = 0; }

void cpp_eh_leave(lexer_t *lx) {
    (void)lx;
    if (g_eh_depth <= 0) return;
    int pop_sym = cpp_eh_sym("__onyx_eh_pop", &ty_void, NULL, NULL);
    for (int i = 0; i < g_eh_depth; i++) cpp_emit_call(pop_sym);
    g_eh_depth = 0;
}
