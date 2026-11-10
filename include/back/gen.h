/*
 * gen.h — code generator + statement/expression parser (single-pass).
 */
#ifndef CC_GEN_H
#define CC_GEN_H

#include "core/cc.h"
#include "front/ast.h"
#include "core/types.h"
#include "front/lexer.h"
#include "back/val.h"

/* Codegen state for current function. */
typedef struct {
    int return_slot;
    int frame_size;        /* total stack frame, aligned 16 */
    int cur_offset;        /* next free slot for locals */
    int max_call_args;     /* max args passed to any call (for stack save) */
    int named_gp, named_fp, named_stack;
    int nparams;           /* number of named parameters */
    bool is_variadic;      /* current function has variadic args */
    int va_save_off;       /* fp offset to GP (a0-a7) register save area (variadic only) */
    int va_fsave_off;      /* fp offset to FP (fa0-fa7) register save area (variadic only) */
    int64_t va_local_slots[8];  /* fp offsets of local va_list objects (16-byte) */
    int n_va_locals;
    int64_t struct_local_slots[64]; /* fp offsets of local struct/union objects */
    int n_struct_locals;
    uint32_t break_label;
    uint32_t continue_label;
    int loop_depth;
    int switch_depth;
    uint32_t break_fixups[256];
    int n_break_fixups;
    uint32_t continue_fixups[256];
    int n_continue_fixups;
    bool in_function;
} gen_func_t;

extern gen_func_t g_func;

/* Initialize codegen. */
void gen_init(void);

/* Reset per-file state (keeps globals, segments, fixups). */
void gen_reset_for_file(void);

/* Generate one top-level declaration. */
void gen_decl(decl_t *d);

/* Finalize: assign global address, resolve entry point. */
void gen_finalize(const char *entry_sym);

/* Parse a global initializer expression (constant only). */
expr_t *gen_parse_global_init(lexer_t *lx, type_t *type);

/* Type parsing — called by parser. Returns true if a type was parsed. */
bool parse_decl_spec(lexer_t *lx, type_t **out_type, bool *out_is_static,
                     bool *out_is_extern, bool *out_is_typedef, bool *out_is_inline,
                     char *tag_out);
type_t *parse_type_spec(lexer_t *lx);

/* Helpers shared with parser. */
bool is_type_start(lexer_t *lx);

int gen_lookup_global(const char *name);
void mangle_static_name(const char *name, char *out, size_t out_sz);

/* Expression/statement machinery exported for the C++ front-end module
 * (Phase A — signatures unchanged from their former static forms). */
val_t parse_expr(lexer_t *lx);
void materialize(val_t *v, int reg);
void load_value(val_t *v, int reg);
void spill_lval(val_t *v);
void spill_ireg(val_t *v);
val_t gen_call(lexer_t *lx, int fn_sym_idx, type_t *fn_type, val_t *callee,
               val_t *hidden_this);

/* Direct call without parsing a source-level argument list — used for
 * implicit C++ default-ctor/dtor calls. `hidden_this` (optional) is
 * passed in a0. Return value is discarded. */
void gen_call0(int fn_sym_idx, val_t *hidden_this);

/* Direct call with exactly one synthesized argument in a0; returns the
 * a0 result typed as fn_type->ret (malloc/free/new/delete helpers). */
val_t gen_call1(int fn_sym_idx, type_t *fn_type, val_t *arg0);

/* Statement parsing (used by C++ try/catch bodies). */
void parse_stmt(lexer_t *lx);

/* Labels and raw branches for C++ statement lowering. */
void gen_label_here(const char *name);
void gen_jump_to(const char *name);
uint32_t gen_emit_bne(int rs1, int rs2);      /* returns patch offset */
void gen_patch_here(uint32_t off);            /* branch lands here */

/* Load the address of global symbol `sym_idx` into `reg` (fixup-based;
 * resolves at finalize time). */
void gen_load_symbol_addr(int reg, int sym_idx);

/* Reserve a global data symbol (address resolves at finalize). */
int gen_reserve_data_symbol(const char *name);

/* Append a vtable to .data and point `sym_idx` at it; entries are
 * function symbol indices (-1 = null). */
void gen_fill_vtable(int sym_idx, const int *fn_syms, int n);

#endif /* CC_GEN_H */
