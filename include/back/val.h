/*
 * val.h — expression value descriptor (val_t) shared by gen.c and the
 * C++ front-end module in src/cpp/.
 *
 * Moved out of src/c/gen.c in Phase A: the cpp module builds values and
 * calls the exported gen helpers (parse_expr, materialize, load_value,
 * spill_lval, spill_ireg, gen_call — see back/gen.h), so val_t must be
 * visible outside gen.c.
 *
 * Value descriptor (val_t):
 *   kind = VAL_REG    result in reg
 *   kind = VAL_IMM    constant value in .imm (use materialize() to load)
 *   kind = VAL_SYM    refers to a symbol (global/local/static) with offset
 *   kind = VAL_LVAL   address in reg, value is at that address (for
 *                     assignment targets and arrays decaying to pointers)
 *
 * Convention: every expression returns VAL_REG by the time it is
 * consumed, except identifiers used as lvalues which return VAL_SYM
 * (then materialized via load_value()).
 */
#ifndef CC_VAL_H
#define CC_VAL_H

#include "core/types.h"

typedef enum {
    VAL_REG,    /* value in .reg */
    VAL_IMM,    /* constant in .imm */
    VAL_SYM,    /* symbol: .sym_idx + .offset */
    VAL_LVAL,   /* address in .reg + .offset */
    VAL_SPILL,  /* address stored at [fp + .offset] (assignment-lhs spill) */
    VAL_SPILL_F, /* FLOAT VALUE stored at [fp + .offset] (binop lhs spill) */
    VAL_SPILL_I, /* INT VALUE stored at [fp + .offset] (binop lhs spill) */
} val_kind_t;

typedef struct {
    val_kind_t kind;
    type_t *type;
    int reg;
    int64_t imm;
    double fval;     /* for float/double constants */
    int sym_idx;     /* for VAL_SYM */
    int64_t offset;  /* for VAL_SYM/VAL_LVAL */
    /* C++ method-group id (marker protocol, spec item 5): 0 = NOT a
     * method — safe default, val_t is zeroed via memset everywhere it
     * is created (e.g. parse_primary); the cpp module allocates group
     * ids starting at 1. See cpp/cpp.h. */
    int cpp_group;
} val_t;

#endif /* CC_VAL_H */
