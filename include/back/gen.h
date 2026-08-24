/*
 * gen.h — code generator + statement/expression parser (single-pass).
 */
#ifndef CC_GEN_H
#define CC_GEN_H

#include "core/cc.h"
#include "front/ast.h"
#include "core/types.h"
#include "front/lexer.h"

/* Codegen state for current function. */
typedef struct {
    int frame_size;        /* total stack frame, aligned 16 */
    int cur_offset;        /* next free slot for locals */
    int max_call_args;     /* max args passed to any call (for stack save) */
    int nparams;           /* number of named parameters */
    bool is_variadic;      /* current function has variadic args */
    int va_save_off;       /* fp offset to register save area (variadic only) */
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

#endif /* CC_GEN_H */
