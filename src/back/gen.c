/*
 * gen.c — single-pass C code generator for RISC-V64.
 *
 * Architecture follows tcc's spirit: parse expression → emit code in
 * one pass. Each expression returns a value descriptor describing
 * where its result lives (in a register by convention).
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
 *
 * For brevity, only the C subset needed for typical userspace
 * programs is supported. C++ is layered on top in cpp.c.
 */
#include "core/compat.h"

#include "core/cc.h"
#include "front/lexer.h"
#include "core/types.h"
#include "front/ast.h"
#include "back/gen.h"
#include "arch/riscv64.h"
#include "front/parser_priv.h"
#include "front/decl.h"

gen_func_t g_func;

/* ---- Fixups for rodata references ----------------------------------- */
typedef struct {
    uint32_t rodata_off;   /* offset within rodata */
    uint32_t patch_lui;
    uint32_t patch_addi;
} rodata_fixup_t;

static rodata_fixup_t g_rodata_fixups[4096];
static int g_n_rodata_fixups = 0;

/* ---- Value descriptor ------------------------------------------------ */
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
} val_t;

/* Forward declarations. */
static val_t parse_assign(lexer_t *lx);
static val_t parse_expr(lexer_t *lx);
static void parse_stmt(lexer_t *lx);
static void parse_stmt_body(lexer_t *lx);
static val_t parse_primary(lexer_t *lx);
static val_t parse_unary(lexer_t *lx);
static long long gen_const_expr(lexer_t *lx);
static void patch_jal(uint32_t off, int32_t delta);
static void patch_sd_imm(uint32_t off, int32_t imm);
static void parse_asm(lexer_t *lx);
/* Current function name — for static-local mangling. */
static char g_cur_func_name[CC_MAX_IDENT];
static type_t *g_cur_func_ret = NULL;   /* return type of enclosing function */

/* Epilogue patch list. Frame layout (fp points at the TOP of the frame,
 * i.e. at the caller's sp at entry):
 *
 *   [fp - frame, fp)      the frame, owned by the callee
 *     fp - 8 .. fp        (unused, keeps locals 16-aligned)
 *     fp - 16 ...         locals / param spill slots (negative offsets)
 *     fp - frame + 8      saved ra
 *     fp - frame + 0      saved fp
 *
 * Epilogue sequence (placeholders patched with the real frame size):
 *     addi sp, fp, -frame   ; sp = frame bottom
 *     ld   ra, 8(sp)
 *     ld   fp, 0(sp)
 *     addi sp, sp, frame    ; sp restored to the top
 *     ret
 *
 * The ORIGINAL code set fp = sp AFTER the stack decrement, so locals at
 * fp-N lived BELOW sp — outside the frame — and any callee prologue
 * overwrote them. Deep call chains also leaked stack because the
 * deallocation was hard-coded to 16.
 */
#define EPILOGUE_PATCH_MAX 512
static uint32_t g_epilogue_patch_off[EPILOGUE_PATCH_MAX];
static uint8_t  g_epilogue_patch_neg[EPILOGUE_PATCH_MAX];  /* 1: imm=-frame, 0: imm=+frame */
static int g_n_epilogue_patches;

static void epilogue_record(uint32_t off, int neg) {
    if (g_n_epilogue_patches < EPILOGUE_PATCH_MAX) {
        g_epilogue_patch_off[g_n_epilogue_patches] = off;
        g_epilogue_patch_neg[g_n_epilogue_patches] = (uint8_t)neg;
        g_n_epilogue_patches++;
    }
}

static void emit_function_epilogue(void) {
    rv_addi(RV_A0, RV_ZERO, 0);
    /* t1 = frame (patched lui+addi pair; single addi cannot hold +2048+). */
    uint32_t lui_off = (uint32_t)g_text.size;
    rv_lui(RV_T1, 0);
    epilogue_record(lui_off, 2);
    uint32_t addi_off = (uint32_t)g_text.size;
    rv_addi(RV_T1, RV_T1, 0);
    epilogue_record(addi_off, 3);
    rv_sub(RV_SP, RV_FP, RV_T1);   /* sp = frame bottom */
    rv_ld(RV_RA, RV_SP, 8);
    rv_ld(RV_FP, RV_SP, 0);
    rv_add(RV_SP, RV_SP, RV_T1);   /* sp = frame top (restored) */
    rv_ret();
}


/* Forward declaration for brace initializer parsing. */
static uint64_t gen_handle_braced_init_global(lexer_t *lx, type_t *type);
static uint64_t gen_emit_global_init_elided(lexer_t *lx, type_t *type);
static void gen_emit_local_braced_init(lexer_t *lx, type_t *type,
                                       int64_t fp_off, int sym_idx);
static void gen_emit_global_init_scalar(lexer_t *lx, type_t *type);
static uint64_t gen_count_brace_elements(lexer_t *lx, type_t *type);

/* Forward declaration for static name mangling. */
static void mangle_static_name(const char *name, char *out, size_t out_sz);

/* Read one or more adjacent string literals and return a malloc'd buffer
 * containing their concatenation.  The lexer is advanced past the last
 * string literal; out_len receives the byte count (excluding NUL).
 * Caller must free the returned pointer. */
static char *parse_concat_string(lexer_t *lx, size_t *out_len) {
    cc_buf_t buf;
    cc_buf_init(&buf);
    while (lx->cur.kind == T_STRING) {
        cc_buf_push(&buf, lx->cur.str, lx->cur.str_len);
        lex_next(lx);
    }
    cc_buf_push8(&buf, 0);
    *out_len = buf.size - 1;
    return (char *)buf.data;
}

/* ---- Label table for goto/label ------------------------------------- */
#define MAX_LABELS 256
#define MAX_PATCHES 256

typedef struct {
    char name[CC_MAX_IDENT];
    uint32_t offset;
    uint32_t patches[MAX_PATCHES];
    int n_patches;
} label_t;

static label_t g_labels[MAX_LABELS];
static int g_n_labels;

static void label_clear(void) {
    g_n_labels = 0;
}

static int label_find_or_add(const char *name) {
    for (int i = 0; i < g_n_labels; i++) {
        if (strcmp(g_labels[i].name, name) == 0) return i;
    }
    if (g_n_labels >= MAX_LABELS) cc_fatal("too many labels");
    int idx = g_n_labels++;
    strncpy(g_labels[idx].name, name, CC_MAX_IDENT - 1);
    g_labels[idx].name[CC_MAX_IDENT - 1] = 0;
    g_labels[idx].offset = 0;
    g_labels[idx].n_patches = 0;
    return idx;
}

static void label_define(const char *name) {
    int idx = label_find_or_add(name);
    label_t *l = &g_labels[idx];
    if (l->offset != 0) {
        cc_error("duplicate label '%s'", name);
        return;
    }
    l->offset = (uint32_t)g_text.size;
    for (int i = 0; i < l->n_patches; i++) {
        int32_t delta = (int32_t)l->offset - (int32_t)l->patches[i];
        patch_jal(l->patches[i], delta);
    }
    l->n_patches = 0;
}

static void label_emit_jump(const char *name) {
    int idx = label_find_or_add(name);
    label_t *l = &g_labels[idx];
    uint32_t patch_off = (uint32_t)g_text.size;
    rv_jal(RV_ZERO, 0);
    if (l->offset != 0) {
        int32_t delta = (int32_t)l->offset - (int32_t)patch_off;
        patch_jal(patch_off, delta);
    } else {
        if (l->n_patches >= MAX_PATCHES) cc_fatal("too many jumps to label");
        l->patches[l->n_patches++] = patch_off;
    }
}

static void label_check_unresolved(void) {
    for (int i = 0; i < g_n_labels; i++) {
        if (g_labels[i].offset == 0 && g_labels[i].n_patches > 0) {
            cc_error("undefined label '%s'", g_labels[i].name);
        }
    }
}

/* ---- Helpers --------------------------------------------------------- */
static int alloc_int_reg(void) {
    /* We use t0/t1/t2 for intermediates, a0 for return values, t3-t6 for
     * spill in complex expressions. For MVP we cap expression complexity
     * at what fits in t0-t6 + a0. */
    return RV_T0;
}

/* Forward decl. */
static void emit_load_global_addr(int reg, int sym_idx, int64_t add_off);

static void materialize(val_t *v, int reg) {
    /* Force v into reg. */
    switch (v->kind) {
        case VAL_IMM:
            rv_addi_imm(reg, RV_ZERO, v->imm);
            v->kind = VAL_REG;
            v->reg = reg;
            break;
        case VAL_SYM: {
            /* Compute address of symbol. */
            bool is_local = (v->sym_idx >= g_n_globals);
            if (is_local) {
                int lidx = v->sym_idx - g_n_globals;
                sym_t *s = &g_locals[lidx];
                rv_addi(reg, RV_FP, (int)s->offset + (int)v->offset);
            } else {
                /* Global: emit lui+addi with fixup. */
                emit_load_global_addr(reg, v->sym_idx, v->offset);
            }
            v->kind = VAL_LVAL;
            v->reg = reg;
            v->offset = 0;
            break;
        }
        case VAL_LVAL:
            /* Address already in v->reg + offset. Move to target reg.
             * When the value already sits in the TARGET register, the
             * offset still has to be folded in with an explicit addi —
             * previously it was silently dropped, so &struct.field
             * returned &struct and every same-register address use
             * pointed at the wrong member. */
            if (v->reg != reg) {
                rv_addi(reg, v->reg, (int)v->offset);
                v->reg = reg;
                v->offset = 0;
            } else if (v->offset != 0) {
                rv_addi(reg, reg, (int)v->offset);
                v->offset = 0;
            }
            break;
        case VAL_SPILL:
            /* Address lives in a stack slot — load it. */
            rv_ld(reg, RV_FP, (int)v->offset);
            v->kind = VAL_LVAL;
            v->reg = reg;
            v->offset = 0;
            break;
        case VAL_REG:
            if (v->reg != reg) {
                rv_mv(reg, v->reg);
                v->reg = reg;
            }
            break;
    }
}

static void load_value(val_t *v, int reg) {
    if (v->kind == VAL_SPILL_I) {
        /* Reload the spilled int value. */
        if (v->type && (v->type->kind == TY_ARRAY || v->type->kind == TY_FUNC)) {
            rv_ld(reg, RV_FP, (int)v->offset);
        } else {
            uint64_t sz = type_sizeof(v->type ? v->type : &ty_long);
            bool uns = type_is_unsigned(v->type);
            switch (sz) {
                case 1: uns ? rv_lbu(reg, RV_FP, (int)v->offset) : rv_lb(reg, RV_FP, (int)v->offset); break;
                case 2: uns ? rv_lhu(reg, RV_FP, (int)v->offset) : rv_lh(reg, RV_FP, (int)v->offset); break;
                case 4: uns ? rv_lwu(reg, RV_FP, (int)v->offset) : rv_lw(reg, RV_FP, (int)v->offset); break;
                default: rv_ld(reg, RV_FP, (int)v->offset); break;
            }
        }
        v->kind = VAL_REG;
        v->reg = reg;
        v->offset = 0;
        return;
    }
    /* Materialize the actual value (dereference if LVAL). */
    materialize(v, reg);
    if (v->kind == VAL_LVAL) {
        /* Load from [reg + offset]. */
        uint64_t sz = type_sizeof(v->type);
        if (v->type->kind == TY_ARRAY || v->type->kind == TY_FUNC) {
            /* Array/function decays to its ADDRESS as a plain VALUE.
             * Returning VAL_LVAL here made consumers (call arguments,
             * casts) deref it again — `(long)buf` passed buf's CONTENTS
             * as the pointer. */
            v->kind = VAL_REG;
            v->offset = 0;
            return;
        }
        switch (sz) {
            case 1: type_is_unsigned(v->type) ? rv_lbu(reg, reg, (int)v->offset) : rv_lb(reg, reg, (int)v->offset); break;
            case 2: type_is_unsigned(v->type) ? rv_lhu(reg, reg, (int)v->offset) : rv_lh(reg, reg, (int)v->offset); break;
            case 4: type_is_unsigned(v->type) ? rv_lwu(reg, reg, (int)v->offset) : rv_lw(reg, reg, (int)v->offset); break;
            case 8: rv_ld(reg, reg, (int)v->offset); break;
            default: rv_ld(reg, reg, (int)v->offset); break;
        }
        v->kind = VAL_REG;
        v->offset = 0;
    }
}

/* Helper: is this a float/double type? */
static bool is_float_type(type_t *t) {
    return t && (t->kind == TY_FLOAT || t->kind == TY_DOUBLE || t->kind == TY_LDOUBLE);
}

/* Materialize a float/double constant into FT0.
 * Strategy: store the bit pattern as an integer constant, load into
 * an int reg, then fmv.w.x (float) or fmv.d.x (double) to move to
 * float reg. For double, we need two lui+addi for the 64-bit pattern,
 * then fmv.d.x.
 * For simplicity, we put the constant in rodata and load via fld/flw.
 */
static void materialize_float_const(val_t *v, int freg) {
    bool is_double = (v->type && v->type->kind == TY_DOUBLE) || v->type->kind == TY_LDOUBLE;
    /* Write the float constant to rodata. */
    uint32_t rodata_off = (uint32_t)g_rodata.size;
    if (is_double) {
        uint64_t bits;
        memcpy(&bits, &v->fval, 8);
        cc_buf_push8(&g_rodata, bits & 0xFF);
        cc_buf_push8(&g_rodata, (bits >> 8) & 0xFF);
        cc_buf_push8(&g_rodata, (bits >> 16) & 0xFF);
        cc_buf_push8(&g_rodata, (bits >> 24) & 0xFF);
        cc_buf_push8(&g_rodata, (bits >> 32) & 0xFF);
        cc_buf_push8(&g_rodata, (bits >> 40) & 0xFF);
        cc_buf_push8(&g_rodata, (bits >> 48) & 0xFF);
        cc_buf_push8(&g_rodata, (bits >> 56) & 0xFF);
    } else {
        float fv = (float)v->fval;
        uint32_t bits;
        memcpy(&bits, &fv, 4);
        cc_buf_push8(&g_rodata, bits & 0xFF);
        cc_buf_push8(&g_rodata, (bits >> 8) & 0xFF);
        cc_buf_push8(&g_rodata, (bits >> 16) & 0xFF);
        cc_buf_push8(&g_rodata, (bits >> 24) & 0xFF);
    }
    /* Emit lui+addi for the rodata address; record fixup. */
    uint32_t lui_off = (uint32_t)g_text.size;
    rv_lui(RV_T0, 0);
    uint32_t addi_off = (uint32_t)g_text.size;
    rv_addi(RV_T0, RV_T0, (int)rodata_off);
    if (g_n_rodata_fixups >= 4096) cc_fatal("too many rodata refs");
    g_rodata_fixups[g_n_rodata_fixups].rodata_off = rodata_off;
    g_rodata_fixups[g_n_rodata_fixups].patch_lui = lui_off;
    g_rodata_fixups[g_n_rodata_fixups].patch_addi = addi_off;
    g_n_rodata_fixups++;
    /* Load float value from rodata address. */
    if (is_double) {
        rv_fld(freg, RV_T0, 0);
    } else {
        rv_flw(freg, RV_T0, 0);
    }
    v->kind = VAL_REG;
    v->reg = freg;
    v->offset = 0;
}

/* Load a float value (like load_value but uses float registers). */
static void load_float_value(val_t *v, int freg) {
    if (v->kind == VAL_IMM && is_float_type(v->type)) {
        /* Float constant — materialize via rodata. */
        materialize_float_const(v, freg);
        return;
    }
    if (v->kind == VAL_IMM && !is_float_type(v->type)) {
        /* Integer constant → float: materialize int, then fcvt.
         * Default to DOUBLE (C promotes int constants in float contexts to
         * double per usual arithmetic conversions); float contexts
         * down-convert at their use site. Using FCVT.S.W here produced a
         * single-precision value whose upper 32 bits were garbage when
         * stored as a double (`double r = 1` read back as 0). */
        materialize(v, RV_T0);
        bool want_double = (freg == RV_FT0) ? true : true;  /* see note */
        (void)want_double;
        rv_fcvt_d_l(freg, RV_T0);
        v->kind = VAL_REG;
        v->reg = freg;
        v->offset = 0;
        /* Keep the value's declared type as double so later stores use FSD. */
        v->type = &ty_double;
        return;
    }
    if (v->kind == VAL_SPILL_F) {
        /* Reload the spilled float value. */
        if (v->type && (v->type->kind == TY_DOUBLE || v->type->kind == TY_LDOUBLE)) {
            rv_fld(freg, RV_FP, (int)v->offset);
        } else {
            rv_flw(freg, RV_FP, (int)v->offset);
        }
        v->kind = VAL_REG;
        v->reg = freg;
        v->offset = 0;
        return;
    }
    if (v->kind == VAL_REG) {
        /* Handle REG FIRST — materialize() would drag the value through
         * an INT register (mv t0, fX), destroying the float bits. */
        if (v->reg == freg) return;
        if (is_float_type(v->type)) {
            /* Move between float regs: fsgnj (fmv within float file). */
            if (v->type && (v->type->kind == TY_DOUBLE || v->type->kind == TY_LDOUBLE)) {
                rv_emit_fpu_r(0x11, 0x0, freg, v->reg, v->reg); /* fsgnj.d */
            } else {
                rv_emit_fpu_r(0x10, 0x0, freg, v->reg, v->reg); /* fsgnj.s */
            }
        } else {
            /* Int reg → float reg via fcvt. */
            if (v->type && (v->type->kind == TY_DOUBLE || v->type->kind == TY_LDOUBLE)) {
                rv_fcvt_d_l(freg, v->reg);
            } else {
                rv_fcvt_s_l(freg, v->reg);
            }
        }
        v->reg = freg;
        return;
    }

    /* For LVAL/SYM: materialize address, then flw/fld — but an INT-typed
     * lvalue must be loaded with lw/lwz first and CONVERTED (fld on a 4-byte
     * int slot read 8 bytes of garbage: log()'s `n * 0.693` exploded). */
    materialize(v, RV_T0);
    if (v->kind == VAL_LVAL) {
        bool is_double = v->type && (v->type->kind == TY_DOUBLE || v->type->kind == TY_LDOUBLE);
        if (is_float_type(v->type)) {
            if (is_double) {
                rv_fld(freg, RV_T0, (int)v->offset);
            } else {
                rv_flw(freg, RV_T0, (int)v->offset);
            }
        } else {
            /* Integer lvalue → float register. */
            uint64_t sz = type_sizeof(v->type);
            bool uns = type_is_unsigned(v->type);
            switch (sz) {
                case 1: uns ? rv_lbu(RV_T1, RV_T0, (int)v->offset) : rv_lb(RV_T1, RV_T0, (int)v->offset); break;
                case 2: uns ? rv_lhu(RV_T1, RV_T0, (int)v->offset) : rv_lh(RV_T1, RV_T0, (int)v->offset); break;
                case 4: uns ? rv_lwu(RV_T1, RV_T0, (int)v->offset) : rv_lw(RV_T1, RV_T0, (int)v->offset); break;
                default: rv_ld(RV_T1, RV_T0, (int)v->offset); break;
            }
            rv_fcvt_d_l(freg, RV_T1);
        }
        v->kind = VAL_REG;
        v->reg = freg;
        v->offset = 0;
        if (!is_float_type(v->type)) {
            v->type = &ty_double;   /* converted value is double */
        }
        return;
    }
}

/* ---- Branch/jump patching helpers ----------------------------------- */
static void patch_branch(uint32_t off, int32_t delta) {
    uint8_t *p = g_text.data + off;
    uint32_t insn = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    /* Clear the B-type immediate bits BEFORE patching — the old code
     * only OR-ed, so a branch patched twice accumulated garbage bits. */
    insn &= ~0xFE000F80u;
    insn |= ((uint32_t)(delta & 0x1000) << 19) |
            ((uint32_t)(delta & 0x7E0) << 20) |
            ((uint32_t)(delta & 0x1E) << 7)  |
            ((uint32_t)(delta & 0x800) >> 4);
    p[0] = insn & 0xff; p[1] = (insn >> 8) & 0xff;
    p[2] = (insn >> 16) & 0xff; p[3] = (insn >> 24) & 0xff;
}

static void patch_jal(uint32_t off, int32_t delta) {
    uint8_t *p = g_text.data + off;
    uint32_t insn = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    insn &= ~0xFFFFF000;
    insn |= ((uint32_t)(delta & 0x100000) << 11) |
            ((uint32_t)(delta & 0x7FE) << 20) |
            ((uint32_t)(delta & 0x800) << 9)  |
            ((uint32_t)(delta & 0xFF000));
    p[0] = insn & 0xff; p[1] = (insn >> 8) & 0xff;
    p[2] = (insn >> 16) & 0xff; p[3] = (insn >> 24) & 0xff;
}

static void patch_sd_imm(uint32_t off, int32_t imm) {
    uint8_t *p = g_text.data + off;
    uint32_t insn = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    insn &= ~0xFE000F80;
    uint32_t imm_lo = (uint32_t)(imm & 0x1F);
    uint32_t imm_hi = (uint32_t)((imm >> 5) & 0x7F);
    insn |= (imm_hi << 25) | (imm_lo << 7);
    p[0] = insn & 0xff; p[1] = (insn >> 8) & 0xff;
    p[2] = (insn >> 16) & 0xff; p[3] = (insn >> 24) & 0xff;
}

/* ---- Type parsing ---------------------------------------------------- */
bool is_type_start(lexer_t *lx) {
    switch (lx->cur.kind) {
        case T_KW_VOID: case T_KW_CHAR: case T_KW_SHORT: case T_KW_INT:
        case T_KW_LONG: case T_KW_FLOAT: case T_KW_DOUBLE: case T_KW_SIGNED:
        case T_KW_UNSIGNED: case T_KW_CONST: case T_KW_VOLATILE:
        case T_KW_STATIC: case T_KW_EXTERN: case T_KW_TYPEDEF: case T_KW_INLINE:
        case T_KW_REGISTER: case T_KW_RESTRICT: case T_KW_AUTO:
        case T_KW_STRUCT: case T_KW_UNION: case T_KW_ENUM:
        case T_KW_BOOL:
            return true;
        case T_IDENT: {
            /* Could be a typedef name. */
            int idx = symtab_lookup_global(lx->cur.text);
            if (idx >= 0 && g_globals[idx].kind == SYM_TYPEDEF) return true;
            return false;
        }
        default:
            return false;
    }
}

type_t *parse_type_spec(lexer_t *lx) {
    /* Collect base type specifiers. */
    bool is_signed = false, is_unsigned = false;
    bool is_short = false, is_long = false, is_longlong = false;
    bool is_const = false, is_volatile = false;
    type_t *base = NULL;
    /* is_signed is parsed for future sign-aware codegen; currently all
     * integer types are treated as their storage size with the sign bit
     * taken from the base type. Kept here so the parser accepts the full
     * C99 type-specifier grammar. */
    (void)is_signed;

    for (;;) {
        switch (lx->cur.kind) {
            case T_KW_CONST:    is_const = true;    lex_next(lx); continue;
            case T_KW_VOLATILE: is_volatile = true; lex_next(lx); continue;
            case T_KW_RESTRICT: lex_next(lx); continue;
            case T_KW_REGISTER: lex_next(lx); continue;
            case T_KW_AUTO:     lex_next(lx); continue;
            case T_KW_SIGNED:   is_signed = true;   lex_next(lx); continue;
            case T_KW_UNSIGNED: is_unsigned = true; lex_next(lx); continue;
            case T_KW_SHORT:    is_short = true;    lex_next(lx); continue;
            case T_KW_LONG:
                if (is_long) { is_longlong = true; is_long = false; }
                else is_long = true;
                lex_next(lx);
                continue;
            case T_KW_VOID:     base = &ty_void;    lex_next(lx); break;
            case T_KW_BOOL:     base = &ty_bool;    lex_next(lx); break;
            case T_KW_CHAR:     base = &ty_char;    lex_next(lx); break;
            case T_KW_INT:      base = &ty_int;     lex_next(lx); break;
            case T_KW_FLOAT:    base = &ty_float;   lex_next(lx); break;
            case T_KW_DOUBLE:
                if (is_long) base = &ty_ldouble;
                else base = &ty_double;
                lex_next(lx);
                break;
            case T_KW_STRUCT:
            case T_KW_UNION: {
                bool is_union = (lx->cur.kind == T_KW_UNION);
                lex_next(lx);
                char tag[CC_MAX_IDENT] = {0};
                if (lx->cur.kind == T_IDENT) {
                    strncpy(tag, lx->cur.text, CC_MAX_IDENT - 1);
                    lex_next(lx);
                }
                type_t *t = tag[0] ? tags_lookup(tag) : NULL;
                if (!t) {
                    t = (type_t *)cc_arena_alloc(&g_type_arena, sizeof(type_t), 8);
                    memset(t, 0, sizeof(*t));
                    t->kind = is_union ? TY_UNION : TY_STRUCT;
                    if (tag[0]) {
                        strncpy(t->tag, tag, CC_MAX_IDENT - 1);
                        tags_install(tag, t);
                    }
                    t->is_complete = false;
                }
                /* Definition: { fields } */
                if (lx->cur.kind == T_LBRACE) {
                    lex_next(lx);
                    uint64_t offset = 0;
                    uint64_t max_align = 1;
                    int nfields = 0;
                    while (lx->cur.kind != T_RBRACE) {
                        type_t *fbase = NULL;
                        bool fs, fe, ft, fi;
                        char ftag[CC_MAX_IDENT] = {0};
                        if (!parse_decl_spec(lx, &fbase, &fs, &fe, &ft, &fi, ftag)) {
                            parse_error("expected field type");
                            break;
                        }
                        /* One or more declarators. */
                        if (lx->cur.kind == T_SEMI) {
                            /* Anonymous struct/union member. */
                            lex_next(lx);
                            continue;
                        }
                        do {
                            char fname[CC_MAX_IDENT] = {0};
                            /* Full declarator: supports function-pointer
                             * fields like `int (*read)(char *, int);`. */
                            type_t *ftyp = parse_declarator(lx, fbase, fname, sizeof(fname));
                            /* Allocate field. */
                            uint64_t falign = type_alignof(ftyp);
                            if (falign > max_align) max_align = falign;
                            if (!is_union) {
                                offset = (offset + falign - 1) & ~(falign - 1);
                            } else {
                                offset = 0;
                            }
                            if (nfields < CC_MAX_STRUCT_FIELDS) {
                                struct_field_t *f = &t->fields[nfields++];
                                strncpy(f->name, fname, CC_MAX_IDENT - 1);
                                f->type = ftyp;
                                f->offset = offset;
                                f->is_anon = (fname[0] == 0);
                            }
                            if (!is_union) {
                                offset += type_sizeof(ftyp);
                            }
                        } while (accept(T_COMMA));
                        parse_expect(T_SEMI, "expected ';'");
                    }
                    parse_expect(T_RBRACE, "expected '}'");
                    t->nfields = nfields;
                    if (is_union) {
                        uint64_t max_size = 0;
                        for (int i = 0; i < nfields; i++) {
                            uint64_t s = type_sizeof(t->fields[i].type);
                            if (s > max_size) max_size = s;
                        }
                        t->size = max_size;
                    } else {
                        t->size = (offset + max_align - 1) & ~(max_align - 1);
                    }
                    t->align = max_align;
                    t->is_complete = true;
                }
                base = t;
                break;
            }
            case T_KW_ENUM: {
                lex_next(lx);
                char tag[CC_MAX_IDENT] = {0};
                if (lx->cur.kind == T_IDENT) {
                    strncpy(tag, lx->cur.text, CC_MAX_IDENT - 1);
                    lex_next(lx);
                }
                type_t *t = tag[0] ? tags_lookup(tag) : NULL;
                if (!t) {
                    t = (type_t *)cc_arena_alloc(&g_type_arena, sizeof(type_t), 8);
                    memset(t, 0, sizeof(*t));
                    t->kind = TY_ENUM;
                    t->base = &ty_int;
                    t->size = 4; t->align = 4; t->is_complete = true;
                    if (tag[0]) {
                        strncpy(t->tag, tag, CC_MAX_IDENT - 1);
                        tags_install(tag, t);
                    }
                }
                if (lx->cur.kind == T_LBRACE) {
                    lex_next(lx);
                    int val = 0;
                    while (lx->cur.kind != T_RBRACE) {
                        if (lx->cur.kind != T_IDENT) break;
                        char ename[CC_MAX_IDENT];
                        strncpy(ename, lx->cur.text, CC_MAX_IDENT - 1);
                        ename[CC_MAX_IDENT - 1] = 0;
                        lex_next(lx);
                        if (accept(T_ASSIGN)) {
                            /* Parse constant expr — MVP: just integer literal. */
                            if (lx->cur.kind == T_INT || lx->cur.kind == T_LONG) {
                                val = (int)lx->cur.ival;
                                lex_next(lx);
                            } else if (lx->cur.kind == T_MINUS) {
                                lex_next(lx);
                                if (lx->cur.kind == T_INT) { val = -(int)lx->cur.ival; lex_next(lx); }
                            }
                        }
                        symtab_install_global(ename, SYM_ENUM_CONST, &ty_int);
                        g_globals[g_n_globals - 1].enum_val = val;
                        val++;
                        if (!accept(T_COMMA)) break;
                    }
                    parse_expect(T_RBRACE, "expected '}'");
                }
                base = t;
                break;
            }
            case T_IDENT: {
                int idx = symtab_lookup_global(lx->cur.text);
                if (idx >= 0 && g_globals[idx].kind == SYM_TYPEDEF) {
                    base = g_globals[idx].type;
                    lex_next(lx);
                    break;
                }
                /* Not a typedef — stop here; apply any long/short/etc. */
                goto finish_type_spec;
            }
            default:
                goto finish_type_spec;
        }
        /* After seeing a base type, we keep collecting qualifiers but break
         * on duplicate base types. */
        if (base) {
            /* Allow combining long/short/signed/unsigned with int/char. */
            if (is_short) {
                base = is_unsigned ? &ty_ushort : &ty_short;
            } else if (is_longlong) {
                base = is_unsigned ? &ty_ullong : &ty_llong;
            } else if (is_long) {
                base = is_unsigned ? &ty_ulong : &ty_long;
            } else if (is_unsigned && base == &ty_char) {
                base = &ty_uchar;
            } else if (is_unsigned && base == &ty_int) {
                base = &ty_uint;
            }
            /* Check for trailing modifiers without new base. */
            if (lx->cur.kind == T_KW_INT) { lex_next(lx); }
            break;
        }
    }

finish_type_spec:
    if (!base) {
        /* Apply long/short/signed/unsigned to implicit int. */
        if (is_short)        base = is_unsigned ? &ty_ushort : &ty_short;
        else if (is_longlong) base = is_unsigned ? &ty_ullong : &ty_llong;
        else if (is_long)    base = is_unsigned ? &ty_ulong  : &ty_long;
        else                 base = &ty_int;
    }
    if (is_const)    base->is_const = true;
    if (is_volatile) base->is_volatile = true;
    return base;
}

bool parse_decl_spec(lexer_t *lx, type_t **out_type, bool *out_is_static,
                     bool *out_is_extern, bool *out_is_typedef, bool *out_is_inline,
                     char *tag_out) {
    bool is_static = false, is_extern = false, is_typedef = false, is_inline = false;
    if (tag_out) tag_out[0] = 0;

    /* Loop on storage class + qualifiers. */
    for (;;) {
        switch (lx->cur.kind) {
            case T_KW_STATIC:   is_static = true;  lex_next(lx); continue;
            case T_KW_EXTERN:   is_extern = true;  lex_next(lx); continue;
            case T_KW_TYPEDEF:  is_typedef = true; lex_next(lx); continue;
            case T_KW_INLINE:   is_inline = true;  lex_next(lx); continue;
            default: break;
        }
        break;
    }

    type_t *base = parse_type_spec(lx);
    if (!base && !is_static && !is_extern && !is_typedef && !is_inline) {
        return false;
    }
    if (!base) base = &ty_int;
    *out_type = base;
    *out_is_static = is_static;
    *out_is_extern = is_extern;
    *out_is_typedef = is_typedef;
    *out_is_inline = is_inline;
    return true;
}

/* ---- Symbol address helpers ----------------------------------------- */
/* Global symbol virtual address — assigned by gen_finalize(). */
typedef struct {
    int sym_idx;
    uint32_t patch_lui;    /* offset of lui instruction */
    uint32_t patch_addi;   /* offset of addi instruction (or 0 if not used) */
    int64_t extra_off;     /* extra offset to add to symbol address */
} global_addr_fixup_t;

static global_addr_fixup_t g_fixups[4096];
static int g_n_fixups = 0;

/* rodata_fixup_t and g_rodata_fixups moved to top of file. */

/* Fixups for global data pointing to rodata (string initializers). */
typedef struct {
    uint32_t rodata_off;   /* offset within rodata */
    uint32_t data_off;     /* offset within g_data to patch (8 bytes, LE) */
} data_rodata_fixup_t;

static data_rodata_fixup_t g_data_rodata_fixups[4096];
static int g_n_data_rodata_fixups = 0;

/* Fixups for global data pointing at another SYMBOL (address-of-global
 * initializers: FILE *stdin = &g_stdin_obj; int *p = arr; void (*f)() = fn). */
typedef struct {
    int      sym_idx;      /* referenced global symbol */
    uint32_t data_off;     /* 8-byte slot in g_data to patch */
} data_sym_fixup_t;

static data_sym_fixup_t g_data_sym_fixups[4096];
static int g_n_data_sym_fixups = 0;

/* Emit a global address load into `reg`. Records a fixup for later.
 * ALWAYS emits the lui+addi pair — the fixup resolver splits the final
 * address into hi20/lo12 across both instructions; a lone lui would
 * silently drop the low 12 bits of the symbol address. */
static void emit_load_global_addr(int reg, int sym_idx, int64_t add_off) {
    uint32_t lui_off = (uint32_t)g_text.size;
    rv_lui(reg, 0);
    uint32_t addi_off = (uint32_t)g_text.size;
    rv_addi(reg, reg, (int)add_off);
    if (g_n_fixups >= 4096) cc_fatal("too many global refs");
    g_fixups[g_n_fixups].sym_idx = sym_idx;
    g_fixups[g_n_fixups].patch_lui = lui_off;
    g_fixups[g_n_fixups].patch_addi = addi_off;
    g_fixups[g_n_fixups].extra_off = add_off;
    g_n_fixups++;
}

/* ---- Function call codegen ------------------------------------------ */
static val_t gen_call(lexer_t *lx, int fn_sym_idx, type_t *fn_type, val_t *callee) {
    /* Parse args. Each argument is evaluated and immediately SPILLED to a
     * stack slot; the a0-a7 registers are loaded from those slots only
     * after every argument has been parsed. Without this, an argument
     * containing a function call (fwrite(s, 1, strlen(s), fp)) would
     * clobber the argument registers already holding earlier arguments —
     * callee-saved registers do not exist in this codegen. */
    parse_expect(T_LPAREN, "expected '('");

    int nargs = 0;
    int64_t arg_slots[8];
    type_t *arg_types[8];
    bool arg_fp[8];

    if (lx->cur.kind != T_RPAREN) {
        for (;;) {
            val_t a = parse_assign(lx);
            if (nargs < 8) {
                bool fp = is_float_type(a.type);
                /* Allocate an 8-byte (16-aligned) spill slot. */
                g_func.cur_offset += 8;
                g_func.cur_offset = (g_func.cur_offset + 15) & ~15;
                int64_t slot = -(int64_t)g_func.cur_offset;
                if (g_func.cur_offset > g_func.frame_size) {
                    g_func.frame_size = g_func.cur_offset;
                }
                bool is_struct = (a.type->kind == TY_STRUCT || a.type->kind == TY_UNION);
                if (fp) {
                    load_float_value(&a, RV_FT0);
                    rv_fsd(RV_FT0, RV_FP, (int)slot);
                } else if (is_struct) {
                    /* Structs (va_list) pass BY VALUE: copy the object into
                     * one contiguous frame block, then pass a POINTER to it
                     * in the argument register (single-register struct ABI). */
                    uint64_t ssz = type_sizeof(a.type);
                    if (ssz > 32) ssz = 32;
                    g_func.cur_offset += ssz;
                    g_func.cur_offset = (g_func.cur_offset + 15) & ~15;
                    int64_t sslot = -(int64_t)g_func.cur_offset;
                    if (g_func.cur_offset > g_func.frame_size) {
                        g_func.frame_size = g_func.cur_offset;
                    }
                    /* Struct PARAMS hold a POINTER (by-value ABI) — local
                     * struct objects are the real thing; deref only params.
                     * NOTE: must classify BEFORE materialize() (which turns
                     * VAL_SYM into VAL_LVAL and hides the slot index). */
                    bool a_is_local = false;
                    if (a.kind == VAL_SYM && a.sym_idx >= g_n_globals) {
                        int64_t aoff = g_locals[a.sym_idx - g_n_globals].offset;
                        for (int vi = 0; vi < g_func.n_struct_locals; vi++) {
                            if (g_func.struct_local_slots[vi] == aoff) {
                                a_is_local = true;
                                break;
                            }
                        }
                    }
                    materialize(&a, RV_T0);       /* &source (or &param slot) */
                    if (!a_is_local) {
                        rv_ld(RV_T0, RV_T0, 0);   /* param slot → object ptr */
                    }
                    for (uint64_t k = 0; k < ssz; k += 8) {
                        rv_ld(RV_T1, RV_T0, (int)k);
                        rv_sd(RV_T1, RV_FP, (int)sslot + (int)k);
                    }
                    /* Load the block's ADDRESS as the argument value. */
                    rv_addi(RV_T0, RV_FP, (int)sslot);
                    rv_sd(RV_T0, RV_FP, (int)slot);
                } else {
                    load_value(&a, RV_T0);
                    rv_sd(RV_T0, RV_FP, (int)slot);
                }
                arg_slots[nargs] = slot;
                arg_types[nargs] = a.type;
                arg_fp[nargs] = fp;
                nargs++;
            }
            if (!accept(T_COMMA)) break;
        }
    }
    parse_expect(T_RPAREN, "expected ')'");

    /* Track max args for stack reservation. */
    if (nargs > g_func.max_call_args) g_func.max_call_args = nargs;

    /* Load arguments into a0-a7 / fa0-fa7 from their spill slots.
     * Per the RISC-V calling convention the two register files are indexed
     * INDEPENDENTLY: the Nth integer arg goes to aN, the Mth FP arg goes to
     * faM — not to fa<total-arg-index>. (fmt, 2.25) used to put 2.25 in fa1.) */
    int gp_idx = 0, fp_idx = 0;
    for (int i = 0; i < nargs && i < 8; i++) {
        if (i >= 8 && gp_idx >= 8 && fp_idx >= 8) break;
        if (arg_fp[i]) {
            if (fp_idx < 8) {
                rv_fld(RV_FA0 + fp_idx, RV_FP, (int)arg_slots[i]);
                fp_idx++;
            }
        } else {
            if (gp_idx < 8) {
                rv_ld(RV_A0 + gp_idx, RV_FP, (int)arg_slots[i]);
                gp_idx++;
            }
        }
    }

    /* Indirect call vs direct call. */
    if (callee && (callee->kind == VAL_REG || callee->kind == VAL_LVAL ||
                   callee->kind == VAL_SYM || callee->kind == VAL_SPILL)) {
        /* Indirect call through function pointer. */
        val_t cv = *callee;
        load_value(&cv, RV_T5);
        rv_jalr(RV_RA, RV_T5, 0);
    } else if (fn_sym_idx >= 0) {
        /* Direct call: la t0, sym; jalr ra, t0, 0. */
        emit_load_global_addr(RV_T0, fn_sym_idx, 0);
        rv_jalr(RV_RA, RV_T0, 0);
    } else {
        cc_error("indirect calls not yet supported");
    }

    /* Return value in a0. */
    val_t result;
    memset(&result, 0, sizeof(result));
    result.kind = VAL_REG;
    result.reg = RV_A0;
    result.type = fn_type ? fn_type->ret : &ty_int;
    return result;
}

/* ---- Postfix operators ---------------------------------------------- */
/* Spill an INT value sitting in a scratch register (T0/T1/T2) to a stack
 * slot — the mirror of spill_freg for the integer side. Without it, an int
 * lhs already materialized in T0 was destroyed by RHS sub-evaluation
 * (2*i*(2*i+1) degenerated to x*x). */
static void spill_ireg(val_t *v) {
    if (v->kind != VAL_REG) return;
    if (v->reg != RV_T0 && v->reg != RV_T1 && v->reg != RV_T2) return;
    if (is_float_type(v->type)) return;
    g_func.cur_offset += 8;
    g_func.cur_offset = (g_func.cur_offset + 15) & ~15;
    int64_t slot = -(int64_t)g_func.cur_offset;
    if (g_func.cur_offset > g_func.frame_size) {
        g_func.frame_size = g_func.cur_offset;
    }
    if (v->type && (v->type->kind == TY_ARRAY || v->type->kind == TY_FUNC)) {
        /* Address-like values: store the register as-is. */
        rv_sd(v->reg, RV_FP, (int)slot);
    } else {
        uint64_t sz = type_sizeof(v->type ? v->type : &ty_long);
        switch (sz) {
            case 1: rv_sb(v->reg, RV_FP, (int)slot); break;
            case 2: rv_sh(v->reg, RV_FP, (int)slot); break;
            case 4: rv_sw(v->reg, RV_FP, (int)slot); break;
            default: rv_sd(v->reg, RV_FP, (int)slot); break;
        }
    }
    v->kind = VAL_SPILL_I;
    v->reg = RV_FP;
    v->offset = slot;
}

/* Spill a float value sitting in a scratch FP register (FT0/FT1) to a
 * stack slot, protecting it across RHS evaluation — the codegen uses FT0
 * as universal scratch, so an FP lhs in FT0 gets destroyed the moment the
 * RHS is parsed. Returns a VAL_SPILL-ish value loaded back on use. */
static void spill_freg(val_t *v) {
    if (v->kind != VAL_REG) return;
    if (v->reg != RV_FT0 && v->reg != RV_FT1) return;
    if (!is_float_type(v->type)) return;
    g_func.cur_offset += 8;
    g_func.cur_offset = (g_func.cur_offset + 15) & ~15;
    int64_t slot = -(int64_t)g_func.cur_offset;
    if (g_func.cur_offset > g_func.frame_size) {
        g_func.frame_size = g_func.cur_offset;
    }
    if (v->type->kind == TY_DOUBLE || v->type->kind == TY_LDOUBLE) {
        rv_fsd(v->reg, RV_FP, (int)slot);
    } else {
        rv_fsw(v->reg, RV_FP, (int)slot);
    }
    v->kind = VAL_SPILL_F;
    v->reg = RV_FP;
    v->offset = slot;
}

/* Spill an LVAL's address register to a stack slot, converting it to
 * VAL_SPILL. Protects the value across subsequent expression evaluation
 * (no register allocator). No-op for non-scratch LVALs. */
static void spill_lval(val_t *v) {
    if (v->kind != VAL_LVAL || v->reg == RV_FP) return;
    g_func.cur_offset += 8;
    g_func.cur_offset = (g_func.cur_offset + 15) & ~15;
    int64_t slot = -(int64_t)g_func.cur_offset;
    if (g_func.cur_offset > g_func.frame_size) {
        g_func.frame_size = g_func.cur_offset;
    }
    if (v->offset == 0) {
        rv_sd(v->reg, RV_FP, (int)slot);
    } else {
        rv_addi(RV_T3, v->reg, (int)v->offset);
        rv_sd(RV_T3, RV_FP, (int)slot);
    }
    v->kind = VAL_SPILL;
    v->reg = RV_FP;
    v->offset = slot;
}

/* Postfix operators: index [], member . ->, call (), ++/--.
 * Shared by primary identifiers and parenthesized expressions. */
static val_t parse_postfix(lexer_t *lx, val_t v) {
            for (;;) {
                if (lx->cur.kind == T_LBRACKET) {
                    /* Protect the base LVAL from being clobbered by the
                     * index expression (fp->buf[fp->buf_end + i] used to
                     * deref through the INDEX value). */
                    spill_lval(&v);
                    lex_next(lx);
                    val_t idx = parse_expr(lx);
                    parse_expect(T_RBRACKET, "expected ']'");
                    /* Materialize the INDEX into T1 first — loading the base
                     * into T0 afterwards used to clobber an index value
                     * sitting in T0 (`set[v % base]` indexed with the base). */
                    load_value(&idx, RV_T1);
                    /* Compute the indexing BASE:
                     * - array lvalue  → address of the array itself
                     * - pointer value → the pointer's VALUE (single deref of
                     *   its slot; load_value would deref the POINT EE too,
                     *   breaking `in[put + i]`). */
                    materialize(&v, RV_T0);
                    if (v.kind == VAL_LVAL && v.type->kind != TY_ARRAY &&
                        v.type->kind != TY_FUNC) {
                        rv_ld(RV_T0, RV_T0, (int)v.offset);
                        v.kind = VAL_REG;
                        v.reg = RV_T0;
                        v.offset = 0;
                    }
                    type_t *elem_type = NULL;
                    if (v.type->kind == TY_ARRAY) {
                        elem_type = v.type->base;
                        uint64_t sz = type_sizeof(elem_type);
                        if (sz != 1) {
                            rv_addi_imm(RV_T2, RV_ZERO, sz);
                            rv_mul(RV_T1, RV_T1, RV_T2);
                        }
                    } else if (v.type->kind == TY_PTR) {
                        elem_type = v.type->base;
                        uint64_t sz = type_sizeof(elem_type);
                        if (sz != 1) {
                            rv_addi_imm(RV_T2, RV_ZERO, sz);
                            rv_mul(RV_T1, RV_T1, RV_T2);
                        }
                    }
                    rv_add(RV_T0, RV_T0, RV_T1);
                    v.kind = VAL_LVAL;
                    v.type = elem_type;
                    v.reg = RV_T0;
                    v.offset = 0;
                    continue;
                }
                if (lx->cur.kind == T_DOT || lx->cur.kind == T_ARROW) {
                    int is_arrow = (lx->cur.kind == T_ARROW);
                    lex_next(lx);
                    if (lx->cur.kind != T_IDENT) {
                        parse_error("expected member name");
                        break;
                    }
                    char mname[CC_MAX_IDENT];
                    strncpy(mname, lx->cur.text, CC_MAX_IDENT - 1);
                    lex_next(lx);
                    /* Get struct type. */
                    type_t *st = is_arrow ? v.type->base : v.type;
                    if (!st || (st->kind != TY_STRUCT && st->kind != TY_UNION)) {
                        cc_error("member access on non-struct");
                        break;
                    }
                    int fi = -1;
                    for (int i = 0; i < st->nfields; i++) {
                        if (strcmp(st->fields[i].name, mname) == 0) { fi = i; break; }
                    }
                    if (fi < 0) {
                        cc_error("no member '%s' in struct", mname);
                        break;
                    }
                    if (is_arrow) {
                        /* v is pointer; load address into T0. */
                        load_value(&v, RV_T0);
                        v.kind = VAL_LVAL;
                        v.reg = RV_T0;
                        v.offset = (int64_t)st->fields[fi].offset;
                    } else {
                        /* v is the struct value (must be LVAL/SYM). */
                        if (v.kind == VAL_SYM) {
                            materialize(&v, RV_T0);
                        }
                        v.kind = VAL_LVAL;
                        v.offset += (int64_t)st->fields[fi].offset;
                    }
                    v.type = st->fields[fi].type;
                    continue;
                }
                if (lx->cur.kind == T_LPAREN) {
                    /* Postfix call on a value: e.g. func_ptr(args), arr[i](args). */
                    type_t *ft = NULL;
                    if (v.type->kind == TY_PTR && v.type->base && v.type->base->kind == TY_FUNC) {
                        ft = v.type->base;
                    } else if (v.type->kind == TY_FUNC) {
                        ft = v.type;
                    }
                    if (!ft) {
                        cc_error("called object is not a function or function pointer");
                        break;
                    }
                    /* The callee address must survive argument evaluation. */
                    spill_lval(&v);
                    return gen_call(lx, -1, ft, &v);
                }
                if (lx->cur.kind == T_INC || lx->cur.kind == T_DEC) {
                    int op = lx->cur.kind;
                    lex_next(lx);
                    if (v.kind != VAL_SYM && v.kind != VAL_LVAL && v.kind != VAL_SPILL) {
                        cc_error("++/-- requires lvalue");
                        break;
                    }
                    /* Spill the object ADDRESS first — loading the current
                     * value into T0 would clobber the address register when
                     * the object itself is an LVAL in a scratch register. */
                    spill_lval(&v);
                    /* Load current value into T0. */
                    val_t cur = v;
                    load_value(&cur, RV_T0);
                    /* Compute new value: +/- 1, scaled by sizeof if pointer. */
                    uint64_t sz = type_is_pointer(v.type) ? type_sizeof(v.type->base) : 1;
                    rv_addi_imm(RV_T1, RV_T0, op == T_INC ? (int64_t)sz : -(int64_t)sz);
                    /* Store back: address is FP-relative (SPILL) or direct. */
                    int addr_reg = RV_T2;
                    if (v.kind == VAL_SYM || v.kind == VAL_SPILL) {
                        materialize(&v, addr_reg);
                    } else {
                        rv_addi(addr_reg, v.reg, (int)v.offset);
                    }
                    uint64_t st = type_sizeof(v.type);
                    switch (st) {
                        case 1: rv_sb(RV_T1, addr_reg, 0); break;
                        case 2: rv_sh(RV_T1, addr_reg, 0); break;
                        case 4: rv_sw(RV_T1, addr_reg, 0); break;
                        case 8: rv_sd(RV_T1, addr_reg, 0); break;
                    }
                    /* Post-inc/dec returns the OLD value (already in T0). */
                    v.kind = VAL_REG;
                    v.reg = RV_T0;
                    v.offset = 0;
                    continue;
                }
                break;
            }
    return v;
}


/* ---- Primary parser ------------------------------------------------- */
static val_t parse_primary(lexer_t *lx) {
    val_t v;
    memset(&v, 0, sizeof(v));
    v.type = &ty_int;

    switch (lx->cur.kind) {
        case T_INT: case T_LONG: case T_UINT: case T_ULONG: {
            v.kind = VAL_IMM;
            v.imm = (int64_t)lx->cur.ival;
            v.type = (lx->cur.kind == T_ULONG) ? &ty_ulong :
                     (lx->cur.kind == T_LONG)  ? &ty_long  :
                     (lx->cur.kind == T_UINT)  ? &ty_uint  : &ty_int;
            lex_next(lx);
            return v;
        }
        case T_FLOAT: case T_DOUBLE: {
            v.kind = VAL_IMM;
            v.fval = lx->cur.fval;
            v.type = (lx->cur.kind == T_FLOAT) ? &ty_float : &ty_double;
            lex_next(lx);
            return v;
        }
        case T_STRING: {
            /* Allocate in rodata; load address into T0 and return VAL_REG. */
            size_t slen;
            char *s = parse_concat_string(lx, &slen);
            uint32_t off = cc_strpool_add(s, slen);
            free(s);
            /* Emit lui+addi for rodata address; record fixup. */
            uint32_t lui_off = (uint32_t)g_text.size;
            rv_lui(RV_T0, 0);
            uint32_t addi_off = (uint32_t)g_text.size;
            rv_addi(RV_T0, RV_T0, (int)off);
            /* Record rodata fixup. */
            if (g_n_rodata_fixups >= 4096) cc_fatal("too many rodata refs");
            g_rodata_fixups[g_n_rodata_fixups].rodata_off = off;
            g_rodata_fixups[g_n_rodata_fixups].patch_lui = lui_off;
            g_rodata_fixups[g_n_rodata_fixups].patch_addi = addi_off;
            g_n_rodata_fixups++;
            v.kind = VAL_REG;
            v.reg = RV_T0;
            v.type = type_make_ptr(&ty_char);
            return v;
        }
        case T_CHAR_LIT: {
            v.kind = VAL_IMM;
            v.imm = (int64_t)lx->cur.ival;
            v.type = &ty_int;
            lex_next(lx);
            return v;
        }
        case T_LPAREN: {
            lex_next(lx);
            /* Could be cast: ( type ) expr. */
            if (is_type_start(lx)) {
                type_t *t = parse_type_spec(lx);
                /* Full abstract declarator: (int *), (char (*)[4]),
                 * (void (*)(int)) — function-pointer casts. */
                char dummy_name[CC_MAX_IDENT];
                t = parse_declarator(lx, t, dummy_name, sizeof(dummy_name));
                parse_expect(T_RPAREN, "expected ')' after cast");
                /* Compound literal: (type){ init_list } */
                if (lx->cur.kind == T_LBRACE) {
                    /* Allocate stack space for the type. */
                    uint64_t sz = type_sizeof(t);
                    uint64_t al = type_alignof(t);
                    g_func.cur_offset += sz;
                    g_func.cur_offset = (g_func.cur_offset + al - 1) & ~(al - 1);
                    int64_t off = -(int64_t)g_func.cur_offset;
                    if (g_func.cur_offset > g_func.frame_size)
                        g_func.frame_size = g_func.cur_offset;
                    /* Compute address of stack slot. */
                    rv_addi(RV_T0, RV_FP, (int)off);
                    /* Parse initializer list and emit stores. */
                    lex_next(lx); /* consume '{' */
                    int field_idx = 0;
                    while (lx->cur.kind != T_RBRACE && lx->cur.kind != T_EOF) {
                        val_t elem = parse_assign(lx);
                        /* Store element at appropriate offset within the struct/array. */
                        uint64_t elem_off = 0;
                        if (t->kind == TY_STRUCT || t->kind == TY_UNION) {
                            /* Walk struct fields to find offset. */
                            type_t *st = t;
                            if (st->nfields > 0 && field_idx < st->nfields) {
                                elem_off = st->fields[field_idx].offset;
                            }
                        } else if (t->kind == TY_ARRAY) {
                            uint64_t elem_sz = type_sizeof(t->base);
                            elem_off = field_idx * elem_sz;
                        }
                        /* Store the element. */
                        load_value(&elem, RV_T1);
                        rv_addi(RV_T2, RV_T0, (int)elem_off);
                        uint64_t elem_sz = type_sizeof(elem.type);
                        switch (elem_sz) {
                            case 1: rv_sb(RV_T1, RV_T2, 0); break;
                            case 2: rv_sh(RV_T1, RV_T2, 0); break;
                            case 4: rv_sw(RV_T1, RV_T2, 0); break;
                            default: rv_sd(RV_T1, RV_T2, 0); break;
                        }
                        field_idx++;
                        if (!accept(T_COMMA)) break;
                    }
                    parse_expect(T_RBRACE, "expected '}' after compound literal");
                    v.kind = VAL_LVAL;
                    v.reg = RV_T0;
                    v.offset = 0;
                    v.type = t;
                    return v;
                }
                val_t e = parse_unary(lx);
                /* Cast handling: int↔float, float↔double, int truncation */
                bool src_float = is_float_type(e.type);
                bool dst_float = is_float_type(t);
                if (src_float && dst_float) {
                    /* float↔double: fcvt.s.d or fcvt.d.s */
                    bool src_d = (e.type->kind == TY_DOUBLE || e.type->kind == TY_LDOUBLE);
                    bool dst_d = (t->kind == TY_DOUBLE || t->kind == TY_LDOUBLE);
                    if (src_d && !dst_d) {
                        /* double → float */
                        load_float_value(&e, RV_FT0);
                        rv_fcvt_s_d(RV_FT1, RV_FT0);
                        e.reg = RV_FT1;
                    } else if (!src_d && dst_d) {
                        /* float → double */
                        load_float_value(&e, RV_FT0);
                        rv_fcvt_d_s(RV_FT1, RV_FT0);
                        e.reg = RV_FT1;
                    } else {
                        /* same size float: just load */
                        load_float_value(&e, RV_FT0);
                        e.reg = RV_FT0;
                    }
                    e.type = t;
                    e.kind = VAL_REG;
                    return e;
                }
                if (src_float && !dst_float) {
                    /* float/double → int: fcvt.l.s or fcvt.l.d */
                    bool src_d = (e.type->kind == TY_DOUBLE || e.type->kind == TY_LDOUBLE);
                    load_float_value(&e, RV_FT0);
                    if (src_d) {
                        rv_fcvt_l_d(RV_T0, RV_FT0);
                    } else {
                        rv_fcvt_l_s(RV_T0, RV_FT0);
                    }
                    /* Truncate to target integer size. */
                    if (type_is_integer(t) && type_sizeof(t) < 8) {
                        uint64_t sz = type_sizeof(t);
                        /* Sign/zero extend handled at store; mask for safety. */
                        if (sz == 4) {
                            rv_slli(RV_T0, RV_T0, 32);
                            rv_srli(RV_T0, RV_T0, 32);
                        } else if (sz == 2) {
                            rv_slli(RV_T0, RV_T0, 48);
                            rv_srli(RV_T0, RV_T0, 48);
                        } else if (sz == 1) {
                            rv_slli(RV_T0, RV_T0, 56);
                            rv_srli(RV_T0, RV_T0, 56);
                        }
                    }
                    e.type = t;
                    e.kind = VAL_REG;
                    e.reg = RV_T0;
                    return e;
                }
                if (!src_float && dst_float) {
                    /* int → float/double: fcvt.s.l or fcvt.d.l */
                    load_value(&e, RV_T0);
                    bool dst_d = (t->kind == TY_DOUBLE || t->kind == TY_LDOUBLE);
                    if (dst_d) {
                        rv_fcvt_d_l(RV_FT0, RV_T0);
                    } else {
                        rv_fcvt_s_l(RV_FT0, RV_T0);
                    }
                    e.type = t;
                    e.kind = VAL_REG;
                    e.reg = RV_FT0;
                    return e;
                }
                /* int → int cast: truncate / extend.
                 * For constant source values, preserve as immediate so that
                 * global initializers like (void*)0 remain constant. */
                if (e.kind == VAL_IMM && !src_float && !dst_float) {
                    uint64_t mask = ~0ULL;
                    uint64_t sz = type_sizeof(t);
                    if (type_is_integer(t) && sz < 8) {
                        mask = (1ULL << (sz * 8)) - 1;
                        if (type_is_signed(t) && (e.imm & (1ULL << (sz * 8 - 1)))) {
                            e.imm |= ~mask;
                        } else {
                            e.imm &= mask;
                        }
                    }
                    e.type = t;
                    return e;
                }
                load_value(&e, RV_T0);
                if (type_is_integer(t) && type_sizeof(t) < 8) {
                    uint64_t sz = type_sizeof(t);
                    if (sz == 4) {
                        if (type_is_signed(t)) {
                            rv_slli(RV_T0, RV_T0, 32);
                            rv_srai(RV_T0, RV_T0, 32);
                        } else {
                            rv_slli(RV_T0, RV_T0, 32);
                            rv_srli(RV_T0, RV_T0, 32);
                        }
                    } else if (sz == 2) {
                        if (type_is_signed(t)) {
                            rv_slli(RV_T0, RV_T0, 48);
                            rv_srai(RV_T0, RV_T0, 48);
                        } else {
                            rv_slli(RV_T0, RV_T0, 48);
                            rv_srli(RV_T0, RV_T0, 48);
                        }
                    } else if (sz == 1) {
                        if (type_is_signed(t)) {
                            rv_slli(RV_T0, RV_T0, 56);
                            rv_srai(RV_T0, RV_T0, 56);
                        } else {
                            rv_slli(RV_T0, RV_T0, 56);
                            rv_srli(RV_T0, RV_T0, 56);
                        }
                    }
                }
                e.type = t;
                e.reg = RV_T0;
                return e;
            }
            val_t e = parse_expr(lx);
            parse_expect(T_RPAREN, "expected ')'");
            /* Postfix operators apply to parenthesized expressions too:
             * (*p)[i], (f())[0], (a.b).c, ... */
            return parse_postfix(lx, e);
        }
        case T_IDENT: {
            char name[CC_MAX_IDENT];
            strncpy(name, lx->cur.text, CC_MAX_IDENT - 1);
            name[CC_MAX_IDENT - 1] = 0;
            cc_pos_t pos = lx->cur.pos;
            lex_next(lx);

            /* Function call? */
            if (lx->cur.kind == T_LPAREN) {
                /* Built-in syscall stubs: __ecall0..6(n[, a[, ...]]) → ecall */
                if (strncmp(name, "__ecall", 7) == 0 && name[7] >= '0' && name[7] <= '6' && name[8] == 0) {
                    int nargs = name[7] - '0';
                    parse_expect(T_LPAREN, "expected '('");
                    int got = 0;
                    if (lx->cur.kind != T_RPAREN) {
                        for (;;) {
                            val_t a = parse_assign(lx);
                            if (got < 7) {
                                int regs[7] = {RV_A7, RV_A0, RV_A1, RV_A2, RV_A3, RV_A4, RV_A5};
                                load_value(&a, regs[got]);
                            }
                            got++;
                            if (!accept(T_COMMA)) break;
                        }
                    }
                    parse_expect(T_RPAREN, "expected ')'");
                    if (got != nargs) {
                        cc_warn("__ecall%d called with %d args", nargs, got);
                    }
                    rv_ecall();
                    val_t r;
                    memset(&r, 0, sizeof(r));
                    r.kind = VAL_REG;
                    r.reg = RV_A0;
                    r.type = &ty_long;
                    return r;
                }
                if (strcmp(name, "va_start") == 0 || strcmp(name, "__builtin_va_start") == 0) {
                    /* va_start(ap, last) — `last` is ignored */
                    parse_expect(T_LPAREN, "expected '('");
                    val_t ap = parse_assign(lx);
                    /* Both spellings take the ignored `last` argument. */
                    if (lx->cur.kind == T_COMMA) {
                        lex_next(lx);
                        parse_assign(lx);
                    }
                    parse_expect(T_RPAREN, "expected ')'");
                    materialize(&ap, RV_T0);
                    if (!g_func.is_variadic) {
                        cc_error("va_start used in non-variadic function");
                    }
                    /* Allocate the 16-byte cursor pair in the frame and
                     * store its ADDRESS into ap. */
                    g_func.cur_offset += 16;
                    g_func.cur_offset = (g_func.cur_offset + 15) & ~15;
                    int64_t pair = -(int64_t)g_func.cur_offset;
                    if (g_func.cur_offset > g_func.frame_size) {
                        g_func.frame_size = g_func.cur_offset;
                    }
                    int gp_off = g_func.nparams * 8;
                    if (gp_off > 64) gp_off = 64;
                    /* pair[0] = &gp_area[nparams] */
                    rv_addi(RV_T1, RV_FP, g_func.va_save_off + gp_off);
                    rv_sd(RV_T1, RV_FP, (int)pair);
                    /* pair[8] = &fp_area[0] (no named FP tracking). */
                    if (g_func.va_fsave_off != 0) {
                        rv_addi(RV_T1, RV_FP, g_func.va_fsave_off);
                        rv_sd(RV_T1, RV_FP, (int)pair + 8);
                    }
                    /* ap = &pair. */
                    rv_addi(RV_T1, RV_FP, (int)pair);
                    rv_sd(RV_T1, RV_T0, 0);
                    val_t vr;
                    memset(&vr, 0, sizeof(vr));
                    return vr;
                }
                if (strcmp(name, "va_arg") == 0 || strcmp(name, "__builtin_va_arg") == 0) {
                    /* va_arg(ap, type) — type is a C type, not an expression */
                    parse_expect(T_LPAREN, "expected '('");
                    val_t ap = parse_assign(lx);
                    parse_expect(T_COMMA, "expected ','");
                    type_t *arg_type = parse_type_spec(lx);
                    /* Full declarator: va_arg(ap, int (*)(void)) etc. */
                    char dummy_name[CC_MAX_IDENT];
                    arg_type = parse_declarator(lx, arg_type, dummy_name, sizeof(dummy_name));
                    parse_expect(T_RPAREN, "expected ')'");
                    /* ap is char* → load the pointer value first. */
                    load_value(&ap, RV_T0);
                    if (is_float_type(arg_type)) {
                        /* FP vararg: cursor lives at pair+8. */
                        rv_ld(RV_T1, RV_T0, 8);
                        rv_fld(RV_FT0, RV_T1, 0);
                        rv_addi(RV_T1, RV_T1, 8);
                        rv_sd(RV_T1, RV_T0, 8);
                        val_t vr;
                        memset(&vr, 0, sizeof(vr));
                        vr.kind = VAL_REG;
                        vr.reg = RV_FT0;
                        vr.type = arg_type;
                        return vr;
                    }
                    rv_ld(RV_T1, RV_T0, 0);
                    uint64_t sz = type_sizeof(arg_type);
                    bool uns = type_is_unsigned(arg_type);
                    switch (sz) {
                        case 1: uns ? rv_lbu(RV_T2, RV_T1, 0) : rv_lb(RV_T2, RV_T1, 0); break;
                        case 2: uns ? rv_lhu(RV_T2, RV_T1, 0) : rv_lh(RV_T2, RV_T1, 0); break;
                        case 4: uns ? rv_lwu(RV_T2, RV_T1, 0) : rv_lw(RV_T2, RV_T1, 0); break;
                        default: rv_ld(RV_T2, RV_T1, 0); break;
                    }
                    rv_addi(RV_T1, RV_T1, 8);
                    rv_sd(RV_T1, RV_T0, 0);
                    val_t vr;
                    memset(&vr, 0, sizeof(vr));
                    vr.kind = VAL_REG;
                    vr.reg = RV_T2;
                    vr.type = arg_type;
                    return vr;
                }
                if (strcmp(name, "va_end") == 0 || strcmp(name, "__builtin_va_end") == 0) {
                    parse_expect(T_LPAREN, "expected '('");
                    parse_assign(lx);
                    parse_expect(T_RPAREN, "expected ')'");
                    val_t vr;
                    memset(&vr, 0, sizeof(vr));
                    return vr;
                }
                /* Check local symbols first (e.g. local function pointer). */
                int lidx = symtab_lookup_local(name);
                if (lidx >= 0 && g_locals[lidx].alias_global >= 0) {
                    /* Static local — indirect call through the backing
                     * global (function-pointer static locals). */
                    int gidx = g_locals[lidx].alias_global;
                    type_t *gt = g_globals[gidx].type;
                    if (gt->kind == TY_PTR && gt->base && gt->base->kind == TY_FUNC) {
                        val_t callee;
                        memset(&callee, 0, sizeof(callee));
                        callee.kind = VAL_SYM;
                        callee.sym_idx = gidx;
                        callee.type = gt;
                        return gen_call(lx, -1, gt->base, &callee);
                    }
                    lidx = -1;
                }
                if (lidx >= 0 && g_locals[lidx].type->kind == TY_PTR &&
                    g_locals[lidx].type->base && g_locals[lidx].type->base->kind == TY_FUNC) {
                    /* Indirect call through local function pointer. */
                    type_t *ft = g_locals[lidx].type->base;
                    val_t callee;
                    memset(&callee, 0, sizeof(callee));
                    callee.kind = VAL_SYM;
                    callee.sym_idx = g_n_globals + lidx;
                    callee.type = g_locals[lidx].type;
                    return gen_call(lx, -1, ft, &callee);
                }

                int idx = symtab_lookup_global(name);
                /* For multi-file compilation, also look up under the mangled
                 * name (static symbols from this file). */
                if (idx < 0 && g_opts.n_input_files > 1) {
                    char mangled[CC_MAX_IDENT + 16];
                    mangle_static_name(name, mangled, sizeof(mangled));
                    idx = symtab_lookup_global(mangled);
                }
                if (idx >= 0 && g_globals[idx].kind == SYM_GLOBAL_VAR &&
                    g_globals[idx].type->kind == TY_PTR &&
                    g_globals[idx].type->base && g_globals[idx].type->base->kind == TY_FUNC) {
                    /* Indirect call through global function pointer variable. */
                    type_t *ft = g_globals[idx].type->base;
                    val_t callee;
                    memset(&callee, 0, sizeof(callee));
                    callee.kind = VAL_SYM;
                    callee.sym_idx = idx;
                    callee.type = g_globals[idx].type;
                    return gen_call(lx, -1, ft, &callee);
                }

                if (idx < 0) {
                    /* Implicit declaration of function — assume int(). */
                    type_t *ft = (type_t *)cc_arena_alloc(&g_type_arena, sizeof(type_t), 8);
                    memset(ft, 0, sizeof(*ft));
                    ft->kind = TY_FUNC;
                    ft->ret = &ty_int;
                    ft->size = 8; ft->align = 8; ft->is_complete = true;
                    idx = symtab_install_global(name, SYM_FUNCTION, ft);
                    g_globals[idx].is_defined = false;
                }
                type_t *ft = g_globals[idx].type;
                return gen_call(lx, idx, ft, NULL);
            }

            /* __func__ — the enclosing function's name as a string. */
            if (strcmp(name, "__func__") == 0) {
                const char *fn = g_cur_func_name[0] ? g_cur_func_name : "?";
                size_t fl = strlen(fn);
                uint32_t off = cc_strpool_add(fn, fl);
                uint32_t lui_off = (uint32_t)g_text.size;
                rv_lui(RV_T0, 0);
                uint32_t addi_off = (uint32_t)g_text.size;
                rv_addi(RV_T0, RV_T0, (int)off);
                if (g_n_rodata_fixups >= 4096) cc_fatal("too many rodata refs");
                g_rodata_fixups[g_n_rodata_fixups].rodata_off = off;
                g_rodata_fixups[g_n_rodata_fixups].patch_lui = lui_off;
                g_rodata_fixups[g_n_rodata_fixups].patch_addi = addi_off;
                g_n_rodata_fixups++;
                v.kind = VAL_REG;
                v.reg = RV_T0;
                v.type = type_make_ptr(&ty_char);
                return v;
            }

            /* Variable / enum const. */
            int lidx = symtab_lookup_local(name);
            if (lidx >= 0 && g_locals[lidx].alias_global >= 0) {
                /* Static local — reference the backing global symbol. */
                v.kind = VAL_SYM;
                v.sym_idx = g_locals[lidx].alias_global;
                v.offset = 0;
                v.type = g_locals[lidx].type;
            } else if (lidx >= 0) {
                v.kind = VAL_SYM;
                v.sym_idx = g_n_globals + lidx;  /* local encoding */
                v.offset = 0;
                v.type = g_locals[lidx].type;
            } else {
                int gidx = symtab_lookup_global(name);
                /* Try mangled name for static symbols from this file. */
                if (gidx < 0 && g_opts.n_input_files > 1) {
                    char mangled[CC_MAX_IDENT + 16];
                    mangle_static_name(name, mangled, sizeof(mangled));
                    gidx = symtab_lookup_global(mangled);
                }
                if (gidx >= 0) {
                    if (g_globals[gidx].kind == SYM_ENUM_CONST) {
                        v.kind = VAL_IMM;
                        v.imm = g_globals[gidx].enum_val;
                        v.type = &ty_int;
                    } else {
                        v.kind = VAL_SYM;
                        v.sym_idx = gidx;
                        v.offset = 0;
                        v.type = g_globals[gidx].type;
                    }
                } else {
                    cc_error_at(pos.file, pos.line, "undeclared identifier '%s'", name);
                    v.kind = VAL_IMM;
                    v.imm = 0;
                }
            }

            /* Postfix: index [], member . ->, call (), ++/--. */
            v = parse_postfix(lx, v);
            return v;
            return v;
        }
        case T_KW_SIZEOF: {
            lex_next(lx);
            parse_expect(T_LPAREN, "expected '(' after sizeof");
            if (is_type_start(lx)) {
                type_t *t = parse_type_spec(lx);
                char dummy_name[CC_MAX_IDENT];
                t = parse_declarator(lx, t, dummy_name, sizeof(dummy_name));
                parse_expect(T_RPAREN, "expected ')'");
                v.kind = VAL_IMM;
                v.imm = (int64_t)type_sizeof(t);
                v.type = &ty_ulong;
                return v;
            } else {
                val_t e = parse_expr(lx);
                parse_expect(T_RPAREN, "expected ')'");
                v.kind = VAL_IMM;
                v.imm = (int64_t)type_sizeof(e.type);
                v.type = &ty_ulong;
                return v;
            }
        }
        default:
            parse_error("expected expression");
            v.kind = VAL_IMM;
            v.imm = 0;
            return v;
    }
}

/* ---- Unary ---------------------------------------------------------- */
static val_t parse_unary(lexer_t *lx) {
    switch (lx->cur.kind) {
        case T_MINUS: {
            lex_next(lx);
            val_t e = parse_unary(lx);
            if (is_float_type(e.type)) {
                load_float_value(&e, RV_FT0);
                bool is_d = (e.type->kind == TY_DOUBLE || e.type->kind == TY_LDOUBLE);
                is_d ? rv_fneg_d(RV_FT0, RV_FT0) : rv_fneg_s(RV_FT0, RV_FT0);
                e.reg = RV_FT0;
            } else {
                load_value(&e, RV_T0);
                rv_neg(RV_T0, RV_T0);
                e.reg = RV_T0;
            }
            return e;
        }
        case T_PLUS: {
            lex_next(lx);
            return parse_unary(lx);
        }
        case T_NOT: {
            lex_next(lx);
            val_t e = parse_unary(lx);
            load_value(&e, RV_T0);
            rv_seqz(RV_T0, RV_T0);
            e.reg = RV_T0;
            e.type = &ty_int;
            return e;
        }
        case T_TILDE: {
            lex_next(lx);
            val_t e = parse_unary(lx);
            load_value(&e, RV_T0);
            rv_not(RV_T0, RV_T0);
            e.reg = RV_T0;
            return e;
        }
        case T_STAR: {
            lex_next(lx);
            val_t e = parse_unary(lx);
            load_value(&e, RV_T0);
            /* e now holds a pointer in T0; result is *T0 as an lvalue. */
            val_t r;
            memset(&r, 0, sizeof(r));
            r.kind = VAL_LVAL;
            r.reg = RV_T0;
            r.offset = 0;
            r.type = e.type->base;
            return r;
        }
        case T_AMP: {
            lex_next(lx);
            val_t e = parse_unary(lx);
            if (e.kind == VAL_SYM) {
                /* &sym — materialize address; the ADDRESS is the value, so
                 * return it as VAL_REG (not LVAL — otherwise gen_store's
                 * load_value would dereference it). */
                materialize(&e, RV_T0);
                e.kind = VAL_REG;
                e.reg = RV_T0;
                e.offset = 0;
                e.type = type_make_ptr(e.type);
                return e;
            }
            if (e.kind == VAL_LVAL || e.kind == VAL_SPILL) {
                /* &*p — same as p; &(a[i]) — address already computed. */
                materialize(&e, RV_T0);
                e.kind = VAL_REG;
                e.reg = RV_T0;
                e.offset = 0;
                e.type = type_make_ptr(e.type);
                return e;
            }
            if (e.kind == VAL_REG && e.type->kind == TY_FUNC) {
                /* Bare function name — decay to function pointer. */
                e.type = type_make_ptr(e.type);
                return e;
            }
            cc_error("cannot take address of value");
            return e;
        }
        case T_INC: case T_DEC: {
            int op = lx->cur.kind;
            lex_next(lx);
            val_t e = parse_unary(lx);
            if (e.kind != VAL_SYM && e.kind != VAL_LVAL && e.kind != VAL_SPILL) {
                cc_error("++/-- requires lvalue");
                return e;
            }
            /* Spill the object ADDRESS before value evaluation. */
            spill_lval(&e);
            /* Load current value. */
            val_t cur = e;
            load_value(&cur, RV_T0);
            /* Compute new value: +/- 1, scaled by sizeof if pointer. */
            uint64_t sz = type_is_pointer(e.type) ? type_sizeof(e.type->base) : 1;
            rv_addi_imm(RV_T1, RV_T0, op == T_INC ? (int64_t)sz : -(int64_t)sz);
            /* Store back. */
            int addr_reg = RV_T2;
            if (e.kind == VAL_SYM || e.kind == VAL_SPILL) {
                materialize(&e, addr_reg);
            } else {
                rv_addi(addr_reg, e.reg, (int)e.offset);
            }
            uint64_t st = type_sizeof(e.type);
            switch (st) {
                case 1: rv_sb(RV_T1, addr_reg, 0); break;
                case 2: rv_sh(RV_T1, addr_reg, 0); break;
                case 4: rv_sw(RV_T1, addr_reg, 0); break;
                case 8: rv_sd(RV_T1, addr_reg, 0); break;
            }
            /* Pre-inc/dec returns the new value. */
            e.kind = VAL_REG;
            e.reg = RV_T0;
            e.offset = 0;
            /* But pre-inc returns NEW value: move T1 to T0. */
            rv_mv(RV_T0, RV_T1);
            return e;
        }
        default:
            return parse_primary(lx);
    }
}

/* Helper: assign a new value to an lvalue. */
static void gen_store(val_t *lhs, val_t *rhs, int op) {
    if (lhs->kind != VAL_SYM && lhs->kind != VAL_LVAL && lhs->kind != VAL_SPILL) {
        cc_error("assignment requires lvalue");
        return;
    }

    bool fp_lhs = is_float_type(lhs->type);

    if (fp_lhs) {
        /* Float/double store path.
         * Load the CURRENT lhs value FIRST: the rhs evaluation leaves its
         * result in FT0, so loading rhs-then-cur clobbers rhs with the cur
         * value (r *= b computed 0 because rhs was overwritten before use). */
        if (op != T_ASSIGN) {
            val_t cur = *lhs;
            load_float_value(&cur, RV_FT1);
            load_float_value(rhs, RV_FT0);
            bool is_d = (lhs->type->kind == TY_DOUBLE || lhs->type->kind == TY_LDOUBLE);
            switch (op) {
                case T_PLUS_ASSIGN:  is_d ? rv_fadd_d(RV_FT0, RV_FT1, RV_FT0) : rv_fadd_s(RV_FT0, RV_FT1, RV_FT0); break;
                case T_MINUS_ASSIGN: is_d ? rv_fsub_d(RV_FT0, RV_FT1, RV_FT0) : rv_fsub_s(RV_FT0, RV_FT1, RV_FT0); break;
                case T_STAR_ASSIGN:  is_d ? rv_fmul_d(RV_FT0, RV_FT1, RV_FT0) : rv_fmul_s(RV_FT0, RV_FT1, RV_FT0); break;
                case T_SLASH_ASSIGN: is_d ? rv_fdiv_d(RV_FT0, RV_FT1, RV_FT0) : rv_fdiv_s(RV_FT0, RV_FT1, RV_FT0); break;
                default: cc_error("invalid compound assignment for float"); break;
            }
        } else {
            load_float_value(rhs, RV_FT0);
        }
        /* Compute address of LHS into T0. */
        int addr_reg = RV_T0;
        if (lhs->kind == VAL_SYM || lhs->kind == VAL_SPILL) {
            materialize(lhs, addr_reg);
        } else {
            rv_addi(addr_reg, lhs->reg, (int)lhs->offset);
        }
        /* Store float reg to memory. */
        bool is_d = (lhs->type->kind == TY_DOUBLE || lhs->type->kind == TY_LDOUBLE);
        if (is_d) {
            rv_fsd(RV_FT0, addr_reg, 0);
        } else {
            rv_fsw(RV_FT0, addr_reg, 0);
        }
        return;
    }

    /* Evaluate RHS into T0. */
    load_value(rhs, RV_T0);

    /* For compound assignments, load current LHS value into T1. */
    int old = RV_T1;
    if (op != T_ASSIGN) {
        val_t cur = *lhs;
        load_value(&cur, old);
    }

    /* Compute new value. */
    switch (op) {
        case T_ASSIGN: break;
        case T_PLUS_ASSIGN:  rv_add(RV_T0, old, RV_T0); break;
        case T_MINUS_ASSIGN: rv_sub(RV_T0, old, RV_T0); break;
        case T_STAR_ASSIGN:  rv_mul(RV_T0, old, RV_T0); break;
        case T_SLASH_ASSIGN: type_is_unsigned(lhs->type) ? rv_divu(RV_T0, old, RV_T0) : rv_div(RV_T0, old, RV_T0); break;
        case T_PERCENT_ASSIGN: type_is_unsigned(lhs->type) ? rv_remu(RV_T0, old, RV_T0) : rv_rem(RV_T0, old, RV_T0); break;
        case T_AND_ASSIGN:   rv_and(RV_T0, old, RV_T0); break;
        case T_OR_ASSIGN:    rv_or(RV_T0, old, RV_T0); break;
        case T_XOR_ASSIGN:   rv_xor(RV_T0, old, RV_T0); break;
        case T_SHL_ASSIGN:   rv_sll(RV_T0, old, RV_T0); break;
        case T_SHR_ASSIGN:   type_is_unsigned(lhs->type) ? rv_srl(RV_T0, old, RV_T0) : rv_sra(RV_T0, old, RV_T0); break;
    }

    /* Compute address of LHS into T2. */
    int addr_reg = RV_T2;
    if (lhs->kind == VAL_SYM || lhs->kind == VAL_SPILL) {
        materialize(lhs, addr_reg);
    } else {
        rv_addi(addr_reg, lhs->reg, (int)lhs->offset);
    }
    uint64_t st = type_sizeof(lhs->type);
    switch (st) {
        case 1: rv_sb(RV_T0, addr_reg, 0); break;
        case 2: rv_sh(RV_T0, addr_reg, 0); break;
        case 4: rv_sw(RV_T0, addr_reg, 0); break;
        case 8: rv_sd(RV_T0, addr_reg, 0); break;
        default: rv_sd(RV_T0, addr_reg, 0); break;
    }
}

/* ---- Binary ops ----------------------------------------------------- */
static val_t parse_binop_rhs(lexer_t *lx, int min_prec, val_t lhs);

static val_t parse_binop(lexer_t *lx) {
    val_t lhs = parse_unary(lx);
    return parse_binop_rhs(lx, 0, lhs);
}

static int op_prec(token_kind_t k) {
    switch (k) {
        case T_STAR: case T_SLASH: case T_PERCENT: return 10;
        case T_PLUS: case T_MINUS: return 9;
        case T_SHL: case T_SHR: return 8;
        case T_LT: case T_GT: case T_LE: case T_GE: return 7;
        case T_EQ: case T_NE: return 6;
        case T_AMP: return 5;
        case T_CARET: return 4;
        case T_PIPE: return 3;
        case T_AND: return 2;
        case T_OR: return 1;
        default: return 0;
    }
}

static val_t parse_binop_rhs(lexer_t *lx, int min_prec, val_t lhs) {
    for (;;) {
        int prec = op_prec(lx->cur.kind);
        if (prec < min_prec || prec == 0) break;
        token_kind_t op = lx->cur.kind;
        lex_next(lx);

        /* Short-circuit for && and ||.
         *
         * Emit pattern (for &&):
         *     <eval lhs>          -> T0
         *     beq T0, zero, Lfalse   ; if lhs==0, jump to Lfalse
         *     <eval rhs>          -> T0
         *     j Lend
         *   Lfalse:
         *     li T0, 0
         *   Lend:
         *
         * For || it's the dual:
         *     <eval lhs>          -> T0
         *     bne T0, zero, Ltrue    ; if lhs!=0, jump to Ltrue
         *     <eval rhs>          -> T0
         *     j Lend
         *   Ltrue:
         *     li T0, 1
         *   Lend:
         *
         * Note: we must NOT recurse into parse_binop_rhs here because the
         * surrounding loop already consumes the operator and we now own the
         * lexer. We parse exactly one RHS operand via parse_binop (which
         * honors precedence) and then continue the outer loop so that
         * subsequent operators at our level are handled correctly.
         */
        if (op == T_AND || op == T_OR) {
            load_value(&lhs, RV_T0);
            if (op == T_AND) {
                /* if lhs==0, jump to false label */
                rv_beq(RV_T0, RV_ZERO, 0);
                uint32_t br_false = (uint32_t)g_text.size - 4;
                /* parse RHS: parse_unary then continue with higher-precedence ops */
                val_t rhs = parse_unary(lx);
                int next_prec = op_prec(lx->cur.kind);
                if (next_prec > prec) {
                    rhs = parse_binop_rhs(lx, prec + 1, rhs);
                }
                load_value(&rhs, RV_T0);
                /* normalize: any non-zero becomes 1 */
                rv_snez(RV_T0, RV_T0);
                /* jump to end */
                rv_jal(RV_ZERO, 0);
                uint32_t jmp_end = (uint32_t)g_text.size - 4;
                /* false: */
                uint32_t false_label = (uint32_t)g_text.size;
                rv_addi(RV_T0, RV_ZERO, 0);
                /* end: */
                uint32_t end_label = (uint32_t)g_text.size;
                /* patch br_false to false_label */
                {
                    int32_t delta = (int32_t)false_label - (int32_t)br_false;
                    uint8_t *p = g_text.data + br_false;
                    uint32_t insn = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                                    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
                    insn |= ((uint32_t)(delta & 0x1000) << 19) |
                            ((uint32_t)(delta & 0x7E0) << 20) |
                            ((uint32_t)(delta & 0x1E) << 7)  |
                            ((uint32_t)(delta & 0x800) >> 4);
                    p[0] = insn & 0xff; p[1] = (insn >> 8) & 0xff;
                    p[2] = (insn >> 16) & 0xff; p[3] = (insn >> 24) & 0xff;
                }
                /* patch jmp_end to end_label */
                {
                    int32_t delta = (int32_t)end_label - (int32_t)jmp_end;
                    uint8_t *p = g_text.data + jmp_end;
                    uint32_t insn = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                                    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
                    /* jal is J-type: imm[20|10:1|11|19:12] at bits 31|30:21|20|19:12 */
                    uint32_t imm = ((uint32_t)((delta >> 20) & 1) << 31) |
                                   ((uint32_t)((delta >> 1) & 0x3FF) << 21) |
                                   ((uint32_t)((delta >> 11) & 1) << 20) |
                                   ((uint32_t)((delta >> 12) & 0xFF) << 12);
                    insn = (insn & 0xFFF) | imm;
                    p[0] = insn & 0xff; p[1] = (insn >> 8) & 0xff;
                    p[2] = (insn >> 16) & 0xff; p[3] = (insn >> 24) & 0xff;
                }
            } else {
                /* if lhs!=0, jump to true label */
                rv_bne(RV_T0, RV_ZERO, 0);
                uint32_t br_true = (uint32_t)g_text.size - 4;
                val_t rhs = parse_unary(lx);
                int next_prec = op_prec(lx->cur.kind);
                if (next_prec > prec) {
                    rhs = parse_binop_rhs(lx, prec + 1, rhs);
                }
                load_value(&rhs, RV_T0);
                rv_snez(RV_T0, RV_T0);
                rv_jal(RV_ZERO, 0);
                uint32_t jmp_end = (uint32_t)g_text.size - 4;
                uint32_t true_label = (uint32_t)g_text.size;
                rv_addi(RV_T0, RV_ZERO, 1);
                uint32_t end_label = (uint32_t)g_text.size;
                /* patch br_true to true_label */
                {
                    int32_t delta = (int32_t)true_label - (int32_t)br_true;
                    uint8_t *p = g_text.data + br_true;
                    uint32_t insn = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                                    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
                    insn |= ((uint32_t)(delta & 0x1000) << 19) |
                            ((uint32_t)(delta & 0x7E0) << 20) |
                            ((uint32_t)(delta & 0x1E) << 7)  |
                            ((uint32_t)(delta & 0x800) >> 4);
                    p[0] = insn & 0xff; p[1] = (insn >> 8) & 0xff;
                    p[2] = (insn >> 16) & 0xff; p[3] = (insn >> 24) & 0xff;
                }
                /* patch jmp_end to end_label */
                {
                    int32_t delta = (int32_t)end_label - (int32_t)jmp_end;
                    uint8_t *p = g_text.data + jmp_end;
                    uint32_t insn = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                                    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
                    uint32_t imm = ((uint32_t)((delta >> 20) & 1) << 31) |
                                   ((uint32_t)((delta >> 1) & 0x3FF) << 21) |
                                   ((uint32_t)((delta >> 11) & 1) << 20) |
                                   ((uint32_t)((delta >> 12) & 0xFF) << 12);
                    insn = (insn & 0xFFF) | imm;
                    p[0] = insn & 0xff; p[1] = (insn >> 8) & 0xff;
                    p[2] = (insn >> 16) & 0xff; p[3] = (insn >> 24) & 0xff;
                }
            }
            /* Result is in T0; treat as int. */
            lhs.kind = VAL_REG;
            lhs.reg = RV_T0;
            lhs.type = &ty_int;
            continue;
        }

        /* Protect an LVAL lhs from RHS evaluation clobbering its address
         * register (no register allocator: `c->pos < c->cap` used to load
         * cap through the pos VALUE). Spill the address to the stack. */
        if (lhs.kind == VAL_LVAL && lhs.reg != RV_FP) {
            g_func.cur_offset += 8;
            g_func.cur_offset = (g_func.cur_offset + 15) & ~15;
            int64_t slot = -(int64_t)g_func.cur_offset;
            if (g_func.cur_offset > g_func.frame_size) {
                g_func.frame_size = g_func.cur_offset;
            }
            if (lhs.offset == 0) {
                rv_sd(lhs.reg, RV_FP, (int)slot);
            } else {
                rv_addi(RV_T3, lhs.reg, (int)lhs.offset);
                rv_sd(RV_T3, RV_FP, (int)slot);
            }
            lhs.kind = VAL_SPILL;
            lhs.reg = RV_FP;
            lhs.offset = slot;
        }

        /* FP lhs sitting in a scratch FP register must be spilled BEFORE
         * the RHS is parsed — RHS evaluation uses FT0/FT1 as scratch and
         * would destroy it (`term * -1.0 / x` yielded the old term). */
        spill_freg(&lhs);
        /* INT lhs in a scratch reg: same rule (2*i*(2*i+1) collapsed to
         * garbage when the RHS sub-expression reused T0). */
        spill_ireg(&lhs);

        val_t rhs = parse_unary(lx);
        int next_prec = op_prec(lx->cur.kind);
        if (next_prec > prec) {
            rhs = parse_binop_rhs(lx, prec + 1, rhs);
        }

        /* Spill an LVAL rhs as well: loading the LHS first would clobber
         * the register holding the RHS address (`c->pos < c->cap`). */
        if (rhs.kind == VAL_LVAL && rhs.reg != RV_FP) {
            g_func.cur_offset += 8;
            g_func.cur_offset = (g_func.cur_offset + 15) & ~15;
            int64_t slot = -(int64_t)g_func.cur_offset;
            if (g_func.cur_offset > g_func.frame_size) {
                g_func.frame_size = g_func.cur_offset;
            }
            if (rhs.offset == 0) {
                rv_sd(rhs.reg, RV_FP, (int)slot);
            } else {
                rv_addi(RV_T3, rhs.reg, (int)rhs.offset);
                rv_sd(RV_T3, RV_FP, (int)slot);
            }
            rhs.kind = VAL_SPILL;
            rhs.reg = RV_FP;
            rhs.offset = slot;
        }

        /* Type promotion for arithmetic. */
        type_t *common = type_common(lhs.type, rhs.type);
        bool fp_arith = is_float_type(common);

        if (fp_arith) {
            /* Float/double arithmetic — use FPU registers.
             * RHS first (results live in FT0 — same clobber rule as ints). */
            bool is_d = (common->kind == TY_DOUBLE || common->kind == TY_LDOUBLE);
            load_float_value(&rhs, RV_FT1);
            load_float_value(&lhs, RV_FT0);
            switch (op) {
                case T_PLUS:
                    is_d ? rv_fadd_d(RV_FT0, RV_FT0, RV_FT1) : rv_fadd_s(RV_FT0, RV_FT0, RV_FT1);
                    break;
                case T_MINUS:
                    is_d ? rv_fsub_d(RV_FT0, RV_FT0, RV_FT1) : rv_fsub_s(RV_FT0, RV_FT0, RV_FT1);
                    break;
                case T_STAR:
                    is_d ? rv_fmul_d(RV_FT0, RV_FT0, RV_FT1) : rv_fmul_s(RV_FT0, RV_FT0, RV_FT1);
                    break;
                case T_SLASH:
                    is_d ? rv_fdiv_d(RV_FT0, RV_FT0, RV_FT1) : rv_fdiv_s(RV_FT0, RV_FT0, RV_FT1);
                    break;
                case T_EQ:
                    is_d ? rv_feq_d(RV_T0, RV_FT0, RV_FT1) : rv_feq_s(RV_T0, RV_FT0, RV_FT1);
                    lhs.kind = VAL_REG; lhs.reg = RV_T0; lhs.type = &ty_int;
                    continue;
                case T_NE:
                    is_d ? rv_feq_d(RV_T0, RV_FT0, RV_FT1) : rv_feq_s(RV_T0, RV_FT0, RV_FT1);
                    /* feq gives 0 or 1; NE = !feq = (feq == 0) = sltiu(rd, rd, 1) */
                    rv_sltiu(RV_T0, RV_T0, 1);
                    lhs.kind = VAL_REG; lhs.reg = RV_T0; lhs.type = &ty_int;
                    continue;
                case T_LT:
                    is_d ? rv_flt_d(RV_T0, RV_FT0, RV_FT1) : rv_flt_s(RV_T0, RV_FT0, RV_FT1);
                    lhs.kind = VAL_REG; lhs.reg = RV_T0; lhs.type = &ty_int;
                    continue;
                case T_GT:
                    is_d ? rv_flt_d(RV_T0, RV_FT1, RV_FT0) : rv_flt_s(RV_T0, RV_FT1, RV_FT0);
                    lhs.kind = VAL_REG; lhs.reg = RV_T0; lhs.type = &ty_int;
                    continue;
                case T_LE:
                    is_d ? rv_fle_d(RV_T0, RV_FT0, RV_FT1) : rv_fle_s(RV_T0, RV_FT0, RV_FT1);
                    lhs.kind = VAL_REG; lhs.reg = RV_T0; lhs.type = &ty_int;
                    continue;
                case T_GE:
                    is_d ? rv_fle_d(RV_T0, RV_FT1, RV_FT0) : rv_fle_s(RV_T0, RV_FT1, RV_FT0);
                    lhs.kind = VAL_REG; lhs.reg = RV_T0; lhs.type = &ty_int;
                    continue;
                default:
                    cc_error("invalid operation on float/double");
                    break;
            }
            lhs.kind = VAL_REG;
            lhs.reg = RV_FT0;
            lhs.type = common;
            continue;
        }

        /* Load RHS first: every expression leaves its result in T0, so
         * loading the LHS into T0 first would clobber an RHS sitting there
         * (`i < n / 2` degenerated into `i < i` and loops never ran). */
        load_value(&rhs, RV_T1);
        load_value(&lhs, RV_T0);

        if (type_is_pointer(lhs.type) && type_is_integer(rhs.type)) {
            /* pointer + int → pointer arithmetic, scale by sizeof(*lhs). */
            uint64_t sz = type_sizeof(lhs.type->base);
            if (sz != 1) {
                rv_addi_imm(RV_T2, RV_ZERO, sz);
                rv_mul(RV_T1, RV_T1, RV_T2);
            }
            common = lhs.type;
        }

        switch (op) {
            case T_PLUS:    rv_add(RV_T0, RV_T0, RV_T1); break;
            case T_MINUS:
                if (type_is_pointer(lhs.type) && type_is_pointer(rhs.type)) {
                    /* pointer - pointer → divide by sizeof. */
                    rv_sub(RV_T0, RV_T0, RV_T1);
                    uint64_t sz = type_sizeof(lhs.type->base);
                    if (sz != 1) {
                        rv_addi_imm(RV_T1, RV_ZERO, sz);
                        rv_divu(RV_T0, RV_T0, RV_T1);
                    }
                    common = &ty_long;
                } else {
                    rv_sub(RV_T0, RV_T0, RV_T1);
                }
                break;
            case T_STAR:    rv_mul(RV_T0, RV_T0, RV_T1); break;
            case T_SLASH:   type_is_unsigned(common) ? rv_divu(RV_T0, RV_T0, RV_T1) : rv_div(RV_T0, RV_T0, RV_T1); break;
            case T_PERCENT: type_is_unsigned(common) ? rv_remu(RV_T0, RV_T0, RV_T1) : rv_rem(RV_T0, RV_T0, RV_T1); break;
            case T_AMP:     rv_and(RV_T0, RV_T0, RV_T1); break;
            case T_PIPE:    rv_or(RV_T0, RV_T0, RV_T1); break;
            case T_CARET:   rv_xor(RV_T0, RV_T0, RV_T1); break;
            case T_SHL:     rv_sll(RV_T0, RV_T0, RV_T1); break;
            case T_SHR:     type_is_unsigned(lhs.type) ? rv_srl(RV_T0, RV_T0, RV_T1) : rv_sra(RV_T0, RV_T0, RV_T1); break;
            case T_EQ:      rv_sub(RV_T0, RV_T0, RV_T1); rv_seqz(RV_T0, RV_T0); break;
            case T_NE:      rv_sub(RV_T0, RV_T0, RV_T1); rv_sltu(RV_T0, RV_ZERO, RV_T0); break;
            case T_LT:      type_is_unsigned(common) ? rv_sltu(RV_T0, RV_T0, RV_T1) : rv_slt(RV_T0, RV_T0, RV_T1); break;
            case T_GT:      type_is_unsigned(common) ? rv_sltu(RV_T0, RV_T1, RV_T0) : rv_slt(RV_T0, RV_T1, RV_T0); break;
            case T_LE:      type_is_unsigned(common) ? rv_sltu(RV_T0, RV_T1, RV_T0) : rv_slt(RV_T0, RV_T1, RV_T0); rv_seqz(RV_T0, RV_T0); break;
            case T_GE:      type_is_unsigned(common) ? rv_sltu(RV_T0, RV_T0, RV_T1) : rv_slt(RV_T0, RV_T0, RV_T1); rv_seqz(RV_T0, RV_T0); break;
            default: break;
        }

        lhs.kind = VAL_REG;
        lhs.reg = RV_T0;
        lhs.type = common;
    }
    return lhs;
}

/* helper for T_NE: invert — unused, kept for future use */
#if 0
static inline void rv_xori_(int rd, int rs) {
    rv_xor(rd, rd, rd);
    rv_addi(rd, rd, 1);
    rv_sub(rd, rd, rs);
}
#endif

/* Ternary. */
static val_t parse_ternary(lexer_t *lx) {
    val_t cond = parse_binop(lx);
    if (accept(T_QUESTION)) {
        load_value(&cond, RV_T0);
        uint32_t else_label = 0;
        uint32_t end_label = 0;
        /* beq t0, zero, else */
        rv_beq(RV_T0, RV_ZERO, 0);
        uint32_t br1_off = (uint32_t)g_text.size - 4;
        val_t then_v = parse_expr(lx);
        if (is_float_type(then_v.type)) {
            load_float_value(&then_v, RV_FT0);
        } else {
            load_value(&then_v, RV_T0);
        }
        /* jal end */
        rv_jal(RV_ZERO, 0);
        uint32_t jmp_off = (uint32_t)g_text.size - 4;
        /* else: */
        else_label = (uint32_t)g_text.size;
        parse_expect(T_COLON, "expected ':' in ternary");
        val_t else_v = parse_ternary(lx);
        if (is_float_type(else_v.type)) {
            load_float_value(&else_v, RV_FT0);
        } else {
            load_value(&else_v, RV_T0);
        }
        end_label = (uint32_t)g_text.size;

        /* Patch branches. */
        int32_t d1 = (int32_t)else_label - (int32_t)br1_off;
        uint8_t *p1 = g_text.data + br1_off;
        uint32_t i1 = (uint32_t)p1[0] | ((uint32_t)p1[1] << 8) | ((uint32_t)p1[2] << 16) | ((uint32_t)p1[3] << 24);
        i1 |= ((uint32_t)(d1 & 0x1000) << 19) | ((uint32_t)(d1 & 0x7E0) << 20) |
              ((uint32_t)(d1 & 0x1E) << 7) | ((uint32_t)(d1 & 0x800) >> 4);
        p1[0] = i1 & 0xff; p1[1] = (i1 >> 8) & 0xff; p1[2] = (i1 >> 16) & 0xff; p1[3] = (i1 >> 24) & 0xff;

        int32_t d2 = (int32_t)end_label - (int32_t)jmp_off;
        uint8_t *p2 = g_text.data + jmp_off;
        uint32_t i2 = (uint32_t)p2[0] | ((uint32_t)p2[1] << 8) | ((uint32_t)p2[2] << 16) | ((uint32_t)p2[3] << 24);
        i2 &= ~0xFFFFF000;
        i2 |= ((uint32_t)(d2 & 0x100000) << 11) | ((uint32_t)(d2 & 0x7FE) << 20) |
              ((uint32_t)(d2 & 0x800) << 9) | ((uint32_t)(d2 & 0xFF000));
        p2[0] = i2 & 0xff; p2[1] = (i2 >> 8) & 0xff; p2[2] = (i2 >> 16) & 0xff; p2[3] = (i2 >> 24) & 0xff;

        then_v.kind = VAL_REG;
        then_v.reg = is_float_type(then_v.type) ? RV_FT0 : RV_T0;
        return then_v;
    }
    return cond;
}

static val_t parse_assign(lexer_t *lx) {
    val_t lhs = parse_ternary(lx);
    token_kind_t k = lx->cur.kind;
    if (k == T_ASSIGN || k == T_PLUS_ASSIGN || k == T_MINUS_ASSIGN || k == T_STAR_ASSIGN ||
        k == T_SLASH_ASSIGN || k == T_PERCENT_ASSIGN || k == T_AND_ASSIGN || k == T_OR_ASSIGN ||
        k == T_XOR_ASSIGN || k == T_SHL_ASSIGN || k == T_SHR_ASSIGN) {
        lex_next(lx);

        /* Spill the LHS address to a stack slot BEFORE evaluating the RHS.
         * The codegen has no register allocator: every expression uses the
         * T0-T2 scratch set, so an LVAL held in a scratch register across
         * RHS evaluation silently turns into garbage (`*p = 5` used to
         * store through the VALUE 5). The spilled slot is marked VAL_SPILL:
         * it CONTAINS the target address (double indirection on use). */
        if (lhs.kind == VAL_LVAL && lhs.reg != RV_FP) {
            g_func.cur_offset += 8;
            g_func.cur_offset = (g_func.cur_offset + 15) & ~15;
            int64_t slot = -(int64_t)g_func.cur_offset;
            if (g_func.cur_offset > g_func.frame_size) {
                g_func.frame_size = g_func.cur_offset;
            }
            if (lhs.offset == 0) {
                rv_sd(lhs.reg, RV_FP, (int)slot);
            } else {
                rv_addi(RV_T3, lhs.reg, (int)lhs.offset);
                rv_sd(RV_T3, RV_FP, (int)slot);
            }
            lhs.kind = VAL_SPILL;
            lhs.reg = RV_FP;
            lhs.offset = slot;
        }

        val_t rhs = parse_assign(lx);
        gen_store(&lhs, &rhs, k);
        /* Assignment returns the assigned value. */
        lhs.kind = VAL_REG;
        lhs.reg = RV_T0;
        lhs.offset = 0;
    }
    return lhs;
}

static val_t parse_expr(lexer_t *lx) {
    lexer_t *saved_g_lx = g_lx;
    g_lx = lx;
    val_t v = parse_assign(lx);
    while (accept(T_COMMA)) {
        v = parse_assign(lx);
    }
    g_lx = saved_g_lx;
    return v;
}

/* ---- Statements ----------------------------------------------------- */

/* Forward. */
static void parse_compound(lexer_t *lx);

/* ---- Inline assembly ------------------------------------------------ */
/* Hex digit value (local copy for asm template parsing). */
static int gen_hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Parse GCC-style inline asm:
 *   asm volatile("template" : outputs : inputs : clobbers);
 *   asm("template");
 * For MVP we only emit the raw instruction bytes from the template string.
 * Operand substitution (%0, %1, ...) and clobbers are ignored for now.
 */
static void parse_asm(lexer_t *lx) {
    lex_next(lx); /* consume 'asm' */
    /* Optional 'volatile' keyword. */
    if (lx->cur.kind == T_KW_VOLATILE) lex_next(lx);
    parse_expect(T_LPAREN, "expected '(' after asm");
    /* Must be a string literal. */
    if (lx->cur.kind != T_STRING) {
        cc_error("expected string literal in asm");
        /* Skip to closing paren. */
        while (lx->cur.kind != T_RPAREN && lx->cur.kind != T_SEMI && lx->cur.kind != T_EOF)
            lex_next(lx);
        if (lx->cur.kind == T_RPAREN) lex_next(lx);
        parse_expect(T_SEMI, "expected ';'");
        return;
    }
    const char *tmpl = lx->cur.str;
    size_t tmpl_len = lx->cur.str_len;
    lex_next(lx);

    /* Skip output/input/clobber operands (everything up to ')'). */
    /* For MVP we just parse : ... : ... : ... but ignore them. */
    while (lx->cur.kind != T_RPAREN && lx->cur.kind != T_EOF) {
        lex_next(lx);
    }
    parse_expect(T_RPAREN, "expected ')' after asm");
    parse_expect(T_SEMI, "expected ';' after asm");

    /* Emit the raw instruction bytes from the template.
     * The template may contain RISC-V instructions encoded as .byte/.word
     * directives, or we support a simple subset of mnemonics.
     * For maximum flexibility, we parse .byte/.word directives in the
     * template and emit the corresponding bytes.  Raw hex like
     *   .word 0x00000013  ->  nop
     * is supported.  Otherwise, we emit each character as-is (which
     * would be wrong for actual asm; this is a placeholder).
     *
     * Most common use: asm volatile(".word 0x...") or asm volatile("nop")
     * We support:
     *   - .word 0xNNNNNNNN  (emit 4 bytes LE)
     *   - .byte 0xNN        (emit 1 byte)
     *   - nop               (emit 0x00000013)
     *   - ecall             (emit 0x00000073)
     *   - ebreak            (emit 0x00100073)
     *   - fence             (emit 0x0FF0000F)
     *   - fence.i           (emit 0x0100000F)
     *   - csr_rcw ...       (placeholder)
     */
    size_t i = 0;
    while (i < tmpl_len) {
        /* Skip whitespace. */
        while (i < tmpl_len && (tmpl[i] == ' ' || tmpl[i] == '\t' || tmpl[i] == '\n' || tmpl[i] == '\r'))
            i++;
        if (i >= tmpl_len) break;

        /* .word directive */
        if (tmpl[i] == '.' && i + 4 < tmpl_len &&
            (tmpl[i+1] == 'w' || tmpl[i+1] == 'W') &&
            (tmpl[i+2] == 'o' || tmpl[i+2] == 'O') &&
            (tmpl[i+3] == 'r' || tmpl[i+3] == 'R') &&
            (tmpl[i+4] == 'd' || tmpl[i+4] == 'D')) {
            i += 5;
            while (i < tmpl_len && tmpl[i] == ' ') i++;
            if (i < tmpl_len && (tmpl[i] == '0') && (i+1 < tmpl_len && (tmpl[i+1] == 'x' || tmpl[i+1] == 'X'))) {
                i += 2;
                uint32_t val = 0;
                while (i < tmpl_len && isxdigit((unsigned char)tmpl[i])) {
                    int d = gen_hexval(tmpl[i]);
                    val = (val << 4) | d;
                    i++;
                }
                /* Emit as 4 bytes little-endian. */
                cc_buf_push8(&g_text, val & 0xFF);
                cc_buf_push8(&g_text, (val >> 8) & 0xFF);
                cc_buf_push8(&g_text, (val >> 16) & 0xFF);
                cc_buf_push8(&g_text, (val >> 24) & 0xFF);
            }
            continue;
        }
        /* .byte directive */
        if (tmpl[i] == '.' && i + 4 < tmpl_len &&
            (tmpl[i+1] == 'b' || tmpl[i+1] == 'B') &&
            (tmpl[i+2] == 'y' || tmpl[i+2] == 'Y') &&
            (tmpl[i+3] == 't' || tmpl[i+3] == 'T') &&
            (tmpl[i+4] == 'e' || tmpl[i+4] == 'E')) {
            i += 5;
            while (i < tmpl_len && tmpl[i] == ' ') i++;
            if (i < tmpl_len && (tmpl[i] == '0') && (i+1 < tmpl_len && (tmpl[i+1] == 'x' || tmpl[i+1] == 'X'))) {
                i += 2;
                uint8_t val = 0;
                while (i < tmpl_len && isxdigit((unsigned char)tmpl[i])) {
                    int d = gen_hexval(tmpl[i]);
                    val = (val << 4) | d;
                    i++;
                }
                cc_buf_push8(&g_text, val);
            }
            continue;
        }
        /* Named mnemonics. */
        if ((tmpl[i] == 'n' || tmpl[i] == 'N') && i + 2 < tmpl_len &&
            (tmpl[i+1] == 'o' || tmpl[i+1] == 'O') &&
            (tmpl[i+2] == 'p' || tmpl[i+2] == 'P')) {
            /* nop */
            cc_buf_push8(&g_text, 0x13); cc_buf_push8(&g_text, 0x00);
            cc_buf_push8(&g_text, 0x00); cc_buf_push8(&g_text, 0x00);
            i += 3;
            continue;
        }
        if ((tmpl[i] == 'e' || tmpl[i] == 'E') && i + 4 < tmpl_len &&
            (tmpl[i+1] == 'c' || tmpl[i+1] == 'C') &&
            (tmpl[i+2] == 'a' || tmpl[i+2] == 'A') &&
            (tmpl[i+3] == 'l' || tmpl[i+3] == 'L') &&
            (tmpl[i+4] == 'l' || tmpl[i+4] == 'L')) {
            /* ecall */
            cc_buf_push8(&g_text, 0x73); cc_buf_push8(&g_text, 0x00);
            cc_buf_push8(&g_text, 0x00); cc_buf_push8(&g_text, 0x00);
            i += 5;
            continue;
        }
        if ((tmpl[i] == 'e' || tmpl[i] == 'E') && i + 5 < tmpl_len &&
            (tmpl[i+1] == 'b' || tmpl[i+1] == 'B') &&
            (tmpl[i+2] == 'r' || tmpl[i+2] == 'R') &&
            (tmpl[i+3] == 'e' || tmpl[i+3] == 'E') &&
            (tmpl[i+4] == 'a' || tmpl[i+4] == 'A') &&
            (tmpl[i+5] == 'k' || tmpl[i+5] == 'K')) {
            /* ebreak */
            cc_buf_push8(&g_text, 0x73); cc_buf_push8(&g_text, 0x10);
            cc_buf_push8(&g_text, 0x00); cc_buf_push8(&g_text, 0x00);
            i += 6;
            continue;
        }
        if ((tmpl[i] == 'f' || tmpl[i] == 'F') && i + 4 < tmpl_len &&
            (tmpl[i+1] == 'e' || tmpl[i+1] == 'E') &&
            (tmpl[i+2] == 'n' || tmpl[i+2] == 'N') &&
            (tmpl[i+3] == 'c' || tmpl[i+3] == 'C') &&
            (tmpl[i+4] == 'e' || tmpl[i+4] == 'E')) {
            /* fence */
            if (i + 5 < tmpl_len && (tmpl[i+5] == '.' || tmpl[i+5] == 'i' || tmpl[i+5] == 'I')) {
                /* fence.i */
                cc_buf_push8(&g_text, 0x0F); cc_buf_push8(&g_text, 0x00);
                cc_buf_push8(&g_text, 0x10); cc_buf_push8(&g_text, 0x00);
                i += 6;
            } else {
                cc_buf_push8(&g_text, 0x0F); cc_buf_push8(&g_text, 0x00);
                cc_buf_push8(&g_text, 0xF0); cc_buf_push8(&g_text, 0x00);
                i += 5;
            }
            continue;
        }
        /* Skip unknown characters until whitespace or next directive. */
        i++;
    }
}

static void parse_stmt_body(lexer_t *lx) {
    cc_pos_t pos = lx->cur.pos;

    /* Label: identifier : */
    if (lx->cur.kind == T_IDENT) {
        lex_peek(lx);
        if (lx->next.kind == T_COLON) {
            char name[CC_MAX_IDENT];
            strncpy(name, lx->cur.text, CC_MAX_IDENT - 1);
            name[CC_MAX_IDENT - 1] = 0;
            lex_next(lx);
            lex_next(lx);
            label_define(name);
            return;
        }
    }

    /* Declarations inside a function are statements (C99). */
    if (is_type_start(lx)) {
        /* Local declaration: parse type + declarators, allocate stack slots. */
        type_t *base = NULL;
        bool is_static, is_extern, is_typedef, is_inline;
        char tag[CC_MAX_IDENT] = {0};
        if (!parse_decl_spec(lx, &base, &is_static, &is_extern, &is_typedef, &is_inline, tag)) {
            parse_error("expected declaration");
            return;
        }
        if (g_lx->cur.kind == T_SEMI) {
            lex_next(lx);
            return;
        }
        for (;;) {
            char name[CC_MAX_IDENT] = {0};
            /* Full declarator — supports function pointers, e.g.
             * `int (*cmp)(const void *, const void *) = my_cmp;`. */
            type_t *t = parse_declarator(lx, base, name, sizeof(name));

            if (is_typedef) {
                /* typedef inside a function body — install globally. */
                t->is_typedef = true;
                symtab_install_global(name, SYM_TYPEDEF, t);
                if (accept(T_COMMA)) continue;
                break;
            }
            if (is_extern) {
                /* extern declaration inside a function — reference to a
                 * global defined elsewhere. */
                t->is_extern = true;
                int gidx = symtab_lookup_global(name);
                if (gidx < 0) {
                    gidx = symtab_install_global(name, SYM_GLOBAL_VAR, t);
                    g_globals[gidx].is_defined = false;
                }
                if (accept(T_COMMA)) continue;
                break;
            }
            if (is_static) {
                /* Static local: allocate in .data/.bss with a mangled name
                 * (function-scoped, collision-free across functions).
                 * Initialization happens at LOAD time like a global, so we
                 * route through gen_decl() instead of emitting runtime
                 * stores. Do NOT set is_static on the type — the name is
                 * already unique (gen_decl's per-file mangling would break
                 * symbol lookups). */
                char gname[CC_MAX_IDENT * 2];
                snprintf(gname, sizeof(gname), "__sl_%s_%s",
                         g_cur_func_name[0] ? g_cur_func_name : "_", name);
                int gidx = symtab_lookup_global(gname);
                if (gidx < 0) {
                    gidx = symtab_install_global(gname, SYM_GLOBAL_VAR, t);
                }
                if (!g_globals[gidx].is_defined) {
                    decl_t *sd = ast_new_decl(D_VAR, lx->cur.pos);
                    sd->type = t;
                    strncpy(sd->name, gname, CC_MAX_IDENT - 1);
                    sd->name[CC_MAX_IDENT - 1] = '\0';
                    if (accept(T_ASSIGN)) {
                        sd->init = gen_parse_global_init(lx, t);
                    }
                    gen_decl(sd);
                } else if (accept(T_ASSIGN)) {
                    /* Redundant re-declaration — consume and ignore init. */
                    gen_parse_global_init(lx, t);
                }
                /* Local alias so plain `name` resolves to the global. */
                int lidx = symtab_lookup_local(name);
                if (lidx < 0) {
                    lidx = symtab_install_local(name, SYM_LOCAL_VAR, t, g_cur_scope);
                }
                g_locals[lidx].alias_global = gidx;
                if (accept(T_COMMA)) continue;
                break;
            }

            /* For incomplete array types, defer stack allocation so that
             * brace initializer can complete the type first. */
            bool deferred = false;
            int64_t off = 0;
            int idx = 0;

            if (t->kind == TY_ARRAY && (t->length == 0 || t->is_incomplete) &&
                lx->cur.kind == T_ASSIGN) {
                deferred = true;
            }

            if (!deferred) {
                uint64_t sz = type_sizeof(t);
                uint64_t al = type_alignof(t);
                g_func.cur_offset += sz;
                g_func.cur_offset = (g_func.cur_offset + al - 1) & ~(al - 1);
                off = -(int64_t)g_func.cur_offset;
                /* Track local struct/union objects: call arguments must
                 * copy them directly (params arrive as pointers). */
                if ((t->kind == TY_STRUCT || t->kind == TY_UNION) &&
                    g_func.n_struct_locals < 64) {
                    g_func.struct_local_slots[g_func.n_struct_locals++] = off;
                }
                idx = symtab_install_local(name, SYM_LOCAL_VAR, t, g_cur_scope);
                g_locals[idx].offset = off;
                if (g_func.cur_offset > g_func.frame_size) {
                    g_func.frame_size = g_func.cur_offset;
                }
            }

            /* Optional initializer. */
            if (accept(T_ASSIGN)) {
                if (t->kind == TY_ARRAY && !deferred && lx->cur.kind == T_STRING &&
                    (t->base->kind == TY_CHAR || t->base->kind == TY_SCHAR ||
                     t->base->kind == TY_UCHAR)) {
                    /* char a[N] = "literal" — copy bytes (with NUL) from
                     * rodata into the local array, zero-pad the rest.
                     * (Previously the string's ADDRESS was stored into
                     * a[0], corrupting the array.) */
                    size_t slen;
                    char *sbuf = parse_concat_string(lx, &slen);
                    uint64_t total = slen + 1;
                    uint64_t arrsz = type_sizeof(t);
                    uint32_t ro_off = cc_strpool_add(sbuf, slen);
                    free(sbuf);
                    uint32_t lui_off = (uint32_t)g_text.size;
                    rv_lui(RV_T1, 0);
                    uint32_t addi_off = (uint32_t)g_text.size;
                    rv_addi(RV_T1, RV_T1, (int)ro_off);
                    if (g_n_rodata_fixups >= 4096) cc_fatal("too many rodata refs");
                    g_rodata_fixups[g_n_rodata_fixups].rodata_off = ro_off;
                    g_rodata_fixups[g_n_rodata_fixups].patch_lui = lui_off;
                    g_rodata_fixups[g_n_rodata_fixups].patch_addi = addi_off;
                    g_n_rodata_fixups++;
                    uint64_t ncopy = total < arrsz ? total : arrsz;
                    for (uint64_t i = 0; i < ncopy; i++) {
                        rv_lbu(RV_T2, RV_T1, (int)i);
                        rv_sb(RV_T2, RV_FP, (int)off + (int)i);
                    }
                    for (uint64_t i = ncopy; i < arrsz; i++) {
                        rv_sb(RV_ZERO, RV_FP, (int)off + (int)i);
                    }
                } else if (lx->cur.kind == T_LBRACE) {
                    if (deferred) {
                        /* For incomplete arrays: parse brace init first to
                         * determine the actual element count, then allocate. */
                        uint64_t elem_count = gen_count_brace_elements(lx, t);
                        /* Complete the array type from element count. */
                        type_t *elem = t->base;
                        uint64_t elem_sz = type_sizeof(elem);
                        uint64_t total_bytes = elem_count * elem_sz;
                        t->length = elem_count;
                        t->is_incomplete = false;
                        t->size = total_bytes;
                        t->is_complete = true;
                        uint64_t sz = type_sizeof(t);
                        uint64_t al = type_alignof(t);
                        g_func.cur_offset += sz;
                        g_func.cur_offset = (g_func.cur_offset + al - 1) & ~(al - 1);
                        off = -(int64_t)g_func.cur_offset;
                        idx = symtab_install_local(name, SYM_LOCAL_VAR, t, g_cur_scope);
                        g_locals[idx].offset = off;
                        if (g_func.cur_offset > g_func.frame_size) {
                            g_func.frame_size = g_func.cur_offset;
                        }
                        /* The lexer was restored to '{' by the counter. Parse again. */
                        gen_emit_local_braced_init(lx, t, off, idx);
                    } else {
                        gen_emit_local_braced_init(lx, t, off, idx);
                    }
                } else if (deferred && lx->cur.kind == T_STRING &&
                           (t->base->kind == TY_CHAR || t->base->kind == TY_SCHAR ||
                            t->base->kind == TY_UCHAR)) {
                    /* char name[] = "literal"; — complete the array from the
                     * string length, allocate, then copy bytes from rodata. */
                    size_t slen;
                    char *sbuf = parse_concat_string(lx, &slen);
                    uint64_t total = slen + 1;   /* include NUL */
                    t->length = total;
                    t->is_incomplete = false;
                    t->size = total;
                    t->is_complete = true;
                    uint64_t al = type_alignof(t);
                    g_func.cur_offset += total;
                    g_func.cur_offset = (g_func.cur_offset + al - 1) & ~(al - 1);
                    off = -(int64_t)g_func.cur_offset;
                    idx = symtab_install_local(name, SYM_LOCAL_VAR, t, g_cur_scope);
                    g_locals[idx].offset = off;
                    if (g_func.cur_offset > g_func.frame_size) {
                        g_func.frame_size = g_func.cur_offset;
                    }
                    /* Bytes live in rodata; emit unrolled copy: lbu/sb pairs. */
                    uint32_t ro_off = cc_strpool_add(sbuf, slen);
                    free(sbuf);
                    uint32_t lui_off = (uint32_t)g_text.size;
                    rv_lui(RV_T1, 0);
                    uint32_t addi_off = (uint32_t)g_text.size;
                    rv_addi(RV_T1, RV_T1, (int)ro_off);
                    if (g_n_rodata_fixups >= 4096) cc_fatal("too many rodata refs");
                    g_rodata_fixups[g_n_rodata_fixups].rodata_off = ro_off;
                    g_rodata_fixups[g_n_rodata_fixups].patch_lui = lui_off;
                    g_rodata_fixups[g_n_rodata_fixups].patch_addi = addi_off;
                    g_n_rodata_fixups++;
                    for (uint64_t i = 0; i < total; i++) {
                        rv_lbu(RV_T2, RV_T1, (int)i);
                        rv_sb(RV_T2, RV_FP, (int)off + (int)i);
                    }
                } else {
                    val_t init = parse_assign(lx);
                    val_t lhs;
                    memset(&lhs, 0, sizeof(lhs));
                    lhs.kind = VAL_SYM;
                    lhs.sym_idx = g_n_globals + idx;
                    lhs.offset = 0;
                    lhs.type = t;
                    gen_store(&lhs, &init, T_ASSIGN);
                }
            }

            if (accept(T_COMMA)) continue;
            break;
        }
        parse_expect(T_SEMI, "expected ';'");
        return;
    }

    switch (lx->cur.kind) {
        case T_SEMI: lex_next(lx); return;
        case T_LBRACE: parse_compound(lx); return;
        case T_KW_ASM: parse_asm(lx); return;
        case T_KW_IF: {
            lex_next(lx);
            parse_expect(T_LPAREN, "expected '(' after if");
            val_t cond = parse_expr(lx);
            parse_expect(T_RPAREN, "expected ')'");
            load_value(&cond, RV_T0);
            /* Inverted-branch pattern (B-type ±4 KiB is too short for big
             * then/else bodies):
             *   bne t0, zero, +8    ; cond true  -> then
             *   jal zero, else/end  ; cond false -> skip (JAL ±1 MiB)
             */
            rv_bne(RV_T0, RV_ZERO, 8);
            rv_jal(RV_ZERO, 0);
            uint32_t br_off = (uint32_t)g_text.size - 4;
            parse_stmt(lx);
            uint32_t end_label;
            if (lx->cur.kind == T_KW_ELSE) {
                lex_next(lx);
                rv_jal(RV_ZERO, 0);
                uint32_t jmp_off = (uint32_t)g_text.size - 4;
                uint32_t else_label = (uint32_t)g_text.size;
                parse_stmt(lx);
                end_label = (uint32_t)g_text.size;
                /* Patch the false-jump to else_label (JAL). */
                patch_jal(br_off, (int32_t)else_label - (int32_t)br_off);
                /* Patch then-skip jump to end_label. */
                patch_jal(jmp_off, (int32_t)end_label - (int32_t)jmp_off);
            } else {
                end_label = (uint32_t)g_text.size;
                /* Patch the false-jump to end_label (JAL). */
                patch_jal(br_off, (int32_t)end_label - (int32_t)br_off);
            }
            return;
        }
        case T_KW_DO: {
            lex_next(lx);
            uint32_t body_label = (uint32_t)g_text.size;
            int saved_n_break = g_func.n_break_fixups;
            int saved_n_cont = g_func.n_continue_fixups;
            uint32_t saved_break = g_func.break_label;
            uint32_t saved_continue = g_func.continue_label;
            g_func.break_label = 0;
            g_func.continue_label = 0;
            g_func.loop_depth++;
            parse_stmt(lx);
            g_func.loop_depth--;
            uint32_t cont_label = (uint32_t)g_text.size;
            g_func.continue_label = cont_label;
            parse_expect(T_KW_WHILE, "expected 'while'");
            parse_expect(T_LPAREN, "expected '('");
            val_t d_cond = parse_expr(lx);
            parse_expect(T_RPAREN, "expected ')'");
            parse_expect(T_SEMI, "expected ';'");
            load_value(&d_cond, RV_T0);
            /* Loop back with JAL (±1 MiB): a big body overflows the B-type
             * backward branch the same way as while-loops. */
            rv_beq(RV_T0, RV_ZERO, 8);   /* cond false -> skip loop-back */
            rv_jal(RV_ZERO, 0);          /* back to body_label */
            uint32_t back_off = (uint32_t)g_text.size - 4;
            uint32_t end_label = (uint32_t)g_text.size;

            patch_jal(back_off, (int32_t)body_label - (int32_t)back_off);
            for (int i = saved_n_break; i < g_func.n_break_fixups; i++)
                patch_jal(g_func.break_fixups[i], (int32_t)end_label - (int32_t)g_func.break_fixups[i]);
            for (int i = saved_n_cont; i < g_func.n_continue_fixups; i++)
                patch_jal(g_func.continue_fixups[i], (int32_t)cont_label - (int32_t)g_func.continue_fixups[i]);
            g_func.n_break_fixups = saved_n_break;
            g_func.n_continue_fixups = saved_n_cont;
            g_func.break_label = saved_break;
            g_func.continue_label = saved_continue;
            return;
        }
        case T_KW_WHILE: {
            lex_next(lx);
            uint32_t cond_label = (uint32_t)g_text.size;
            parse_expect(T_LPAREN, "expected '('");
            val_t cond = parse_expr(lx);
            parse_expect(T_RPAREN, "expected ')'");
            load_value(&cond, RV_T0);
            /* Inverted-branch pattern: B-type immediates only reach ±4 KiB;
             * loop bodies can be far larger (vformat_run's switch is >8 KiB),
             * which used to overflow the immediate into a BACKWARD jump.
             *   bne t0, zero, +8     ; cond true -> fall into body
             *   jal zero, exit       ; cond false -> exit (JAL: ±1 MiB)
             */
            rv_bne(RV_T0, RV_ZERO, 8);
            rv_jal(RV_ZERO, 0);
            uint32_t br_off = (uint32_t)g_text.size - 4;
            int saved_n_break = g_func.n_break_fixups;
            int saved_n_cont = g_func.n_continue_fixups;
            uint32_t saved_break = g_func.break_label;
            uint32_t saved_continue = g_func.continue_label;
            g_func.break_label = 0;
            g_func.continue_label = cond_label;
            g_func.loop_depth++;
            parse_stmt(lx);
            g_func.loop_depth--;
            rv_jal(RV_ZERO, 0);  /* back to cond_label */
            uint32_t jmp_off = (uint32_t)g_text.size - 4;
            uint32_t end_label = (uint32_t)g_text.size;
            /* Patch conditional exit jump. */
            patch_jal(br_off, (int32_t)end_label - (int32_t)br_off);
            /* Patch loop-back jump. */
            patch_jal(jmp_off, (int32_t)cond_label - (int32_t)jmp_off);
            /* Patch break fixups. */
            for (int i = saved_n_break; i < g_func.n_break_fixups; i++)
                patch_jal(g_func.break_fixups[i], (int32_t)end_label - (int32_t)g_func.break_fixups[i]);
            /* Patch continue fixups (shouldn't be any for while since
             * continue_label is known, but handle for safety). */
            for (int i = saved_n_cont; i < g_func.n_continue_fixups; i++)
                patch_jal(g_func.continue_fixups[i], (int32_t)cond_label - (int32_t)g_func.continue_fixups[i]);
            g_func.n_break_fixups = saved_n_break;
            g_func.n_continue_fixups = saved_n_cont;
            g_func.break_label = saved_break;
            g_func.continue_label = saved_continue;
            return;
        }
        case T_KW_RETURN: {
            lex_next(lx);
            if (lx->cur.kind != T_SEMI) {
                val_t v = parse_expr(lx);
                if (is_float_type(v.type)) {
                    /* Return float/double in FA0 (float ABI convention). */
                    load_float_value(&v, RV_FA0);
                } else if (g_cur_func_ret && is_float_type(g_cur_func_ret)) {
                    /* Int value returned from a double function: convert
                     * (`return 1` in pow() used to leave fa0 untouched). */
                    load_value(&v, RV_T0);
                    rv_fcvt_d_l(RV_FA0, RV_T0);
                } else {
                    load_value(&v, RV_A0);
                }
            }
            parse_expect(T_SEMI, "expected ';'");
            /* Epilogue: restore fp, ra; dealloc stack; ret.
             * Return value already in a0/fa0 — keep it. */
            {
                uint32_t lui_off = (uint32_t)g_text.size;
                rv_lui(RV_T1, 0);
                epilogue_record(lui_off, 2);
                uint32_t addi_off = (uint32_t)g_text.size;
                rv_addi(RV_T1, RV_T1, 0);
                epilogue_record(addi_off, 3);
                rv_sub(RV_SP, RV_FP, RV_T1);
                rv_ld(RV_RA, RV_SP, 8);
                rv_ld(RV_FP, RV_SP, 0);
                rv_add(RV_SP, RV_SP, RV_T1);
                rv_ret();
            }
            return;
        }
        case T_KW_BREAK: {
            lex_next(lx);
            parse_expect(T_SEMI, "expected ';'");
            if (g_func.loop_depth == 0 && g_func.switch_depth == 0) {
                cc_error("'break' outside loop or switch");
                return;
            }
            rv_jal(RV_ZERO, 0);
            uint32_t br_off = (uint32_t)g_text.size - 4;
            g_func.break_fixups[g_func.n_break_fixups++] = br_off;
            return;
        }
        case T_KW_CONTINUE:
            lex_next(lx);
            parse_expect(T_SEMI, "expected ';'");
            if (g_func.loop_depth == 0) {
                cc_error("'continue' outside loop");
                return;
            }
            rv_jal(RV_ZERO, 0);
            {
                uint32_t cont_off = (uint32_t)g_text.size - 4;
                if (g_func.continue_label != 0) {
                    /* Target already known (e.g. while loop) — patch directly. */
                    patch_jal(cont_off, (int32_t)g_func.continue_label - (int32_t)cont_off);
                } else {
                    /* Target not yet known (e.g. for loop post-expr) — add fixup. */
                    g_func.continue_fixups[g_func.n_continue_fixups++] = cont_off;
                }
            }
            return;
        case T_KW_GOTO: {
            lex_next(lx);
            if (lx->cur.kind != T_IDENT) {
                parse_error("expected label name");
                return;
            }
            char name[CC_MAX_IDENT];
            strncpy(name, lx->cur.text, CC_MAX_IDENT - 1);
            name[CC_MAX_IDENT - 1] = 0;
            lex_next(lx);
            parse_expect(T_SEMI, "expected ';'");
            label_emit_jump(name);
            return;
        }
        case T_KW_FOR: {
            /* for(init; cond; post) body — single-pass codegen.
             * Emit: init → cond → [branch if !cond to end] → body → post → jump to cond.
             * The post-expression is skipped during initial lexing (source range
             * saved), then re-lexed and parsed after the body so its code is
             * emitted in the correct position. */
            lex_next(lx);
            parse_expect(T_LPAREN, "expected '(' after for");
            symtab_push_scope();
            /* init */
            if (lx->cur.kind != T_SEMI) {
                parse_stmt(lx);  /* will consume ';' */
            } else {
                lex_next(lx);
            }
            uint32_t cond_label = (uint32_t)g_text.size;
            val_t cond;
            bool has_cond = false;
            if (lx->cur.kind != T_SEMI) {
                cond = parse_expr(lx);
                has_cond = true;
            }
            parse_expect(T_SEMI, "expected ';'");
            if (has_cond) {
                load_value(&cond, RV_T0);
                /* Inverted-branch exit (B-type ±4 KiB overflows on big
                 * bodies — same fix as while). */
                rv_bne(RV_T0, RV_ZERO, 8);
                rv_jal(RV_ZERO, 0);
            }
            uint32_t br_off = has_cond ? (uint32_t)g_text.size - 4 : 0;

            /* Save post-expression source range, then skip it.
             * Use token byte offsets rather than lx->pos because lx->pos may
             * be ahead due to one-token lookahead. */
            size_t post_start_off = lx->cur.src_off;
            int paren = 0;
            while (lx->cur.kind != T_RPAREN || paren > 0) {
                if (lx->cur.kind == T_LPAREN) paren++;
                else if (lx->cur.kind == T_RPAREN) paren--;
                else if (lx->cur.kind == T_EOF) break;
                lex_next(lx);
            }
            size_t post_end_off = lx->cur.src_off;
            parse_expect(T_RPAREN, "expected ')'");

            int saved_n_break = g_func.n_break_fixups;
            int saved_n_cont = g_func.n_continue_fixups;
            uint32_t saved_break = g_func.break_label;
            uint32_t saved_continue = g_func.continue_label;
            g_func.break_label = 0;
            /* continue_label is not known yet (post-expr hasn't been emitted),
             * so set to 0 to signal that continue fixups are needed. */
            g_func.continue_label = 0;
            g_func.loop_depth++;
            parse_stmt(lx);

            /* Continue target: emit post-expression here. */
            uint32_t post_label = (uint32_t)g_text.size;
            if (post_end_off > post_start_off) {
                /* Re-lex and parse the post-expression from the saved source
                 * range using a temporary lexer.  The symbol table is shared
                 * (global state) so variable lookups still work.
                 * We must also swap g_lx because parse_error/parse_expect/
                 * accept use the global lexer pointer. */
                lexer_t post_lx;
                lexer_t *saved_g_lx = g_lx;
                lex_init(&post_lx, lx->src + post_start_off,
                         post_end_off - post_start_off, lx->filename);
                /* lex_init already primes cur with the first token. */
                g_lx = &post_lx;
                val_t pv = parse_expr(&post_lx);
                (void)pv;
                g_lx = saved_g_lx;
            }


            rv_jal(RV_ZERO, 0);
            uint32_t jmp_off = (uint32_t)g_text.size - 4;
            uint32_t end_label = (uint32_t)g_text.size;
            g_func.loop_depth--;

            if (has_cond)
                patch_jal(br_off, (int32_t)end_label - (int32_t)br_off);
            patch_jal(jmp_off, (int32_t)cond_label - (int32_t)jmp_off);
            /* Patch break fixups → end_label. */
            for (int i = saved_n_break; i < g_func.n_break_fixups; i++)
                patch_jal(g_func.break_fixups[i], (int32_t)end_label - (int32_t)g_func.break_fixups[i]);
            /* Patch continue fixups → post_label. */
            for (int i = saved_n_cont; i < g_func.n_continue_fixups; i++)
                patch_jal(g_func.continue_fixups[i], (int32_t)post_label - (int32_t)g_func.continue_fixups[i]);
            g_func.n_break_fixups = saved_n_break;
            g_func.n_continue_fixups = saved_n_cont;
            g_func.break_label = saved_break;
            g_func.continue_label = saved_continue;
            symtab_pop_scope();
            return;
        }
        case T_KW_SWITCH: {
            lex_next(lx);
            parse_expect(T_LPAREN, "expected '(' after switch");
            val_t sv = parse_expr(lx);
            parse_expect(T_RPAREN, "expected ')'");
            load_value(&sv, RV_T0);
            rv_mv(RV_T1, RV_T0);
            parse_expect(T_LBRACE, "expected '{'");
            uint32_t saved_break = g_func.break_label;
            int saved_n_break = g_func.n_break_fixups;
            g_func.break_label = 0;
            g_func.switch_depth++;
            uint32_t prev_branch = 0;
            /* Label-to-handler jumps: every `case V:` compare that MATCHES
             * must jump past the remaining labels of its group into the
             * handler. The old code relied on fall-through, so for
             * `case 'd': case 'i': handler` a 'd' match fell into the 'i'
             * compare, mismatched, and escaped to the NEXT case — args were
             * consumed by the wrong handlers (printf %d/%u/%x shifted). */
            uint32_t label_jumps[128];
            int n_label_jumps = 0;
            while (lx->cur.kind != T_RBRACE && lx->cur.kind != T_EOF) {
                if (lx->cur.kind == T_KW_CASE) {
                    if (prev_branch)
                        patch_branch(prev_branch, (int32_t)g_text.size - (int32_t)prev_branch);
                    lex_next(lx);
                    val_t cv = parse_expr(lx);
                    load_value(&cv, RV_T0);
                    parse_expect(T_COLON, "expected ':' after case value");
                    rv_bne(RV_T1, RV_T0, 0);
                    prev_branch = (uint32_t)g_text.size - 4;
                    /* match → jump to handler (patched below). */
                    rv_jal(RV_ZERO, 0);
                    if (n_label_jumps < 128)
                        label_jumps[n_label_jumps++] = (uint32_t)g_text.size - 4;
                } else if (lx->cur.kind == T_KW_DEFAULT) {
                    if (prev_branch)
                        patch_branch(prev_branch, (int32_t)g_text.size - (int32_t)prev_branch);
                    prev_branch = 0;
                    lex_next(lx);
                    parse_expect(T_COLON, "expected ':' after default");
                    /* default: the handler starts right here. */
                    for (int i = 0; i < n_label_jumps; i++)
                        patch_jal(label_jumps[i], (int32_t)g_text.size - (int32_t)label_jumps[i]);
                    n_label_jumps = 0;
                } else {
                    /* First statement of this label group = handler start. */
                    if (n_label_jumps > 0) {
                        for (int i = 0; i < n_label_jumps; i++)
                            patch_jal(label_jumps[i], (int32_t)g_text.size - (int32_t)label_jumps[i]);
                        n_label_jumps = 0;
                    }
                    parse_stmt(lx);
                }
            }
            /* Empty trailing group: handler = switch end. */
            for (int i = 0; i < n_label_jumps; i++)
                patch_jal(label_jumps[i], (int32_t)g_text.size - (int32_t)label_jumps[i]);
            if (prev_branch)
                patch_branch(prev_branch, (int32_t)g_text.size - (int32_t)prev_branch);
            uint32_t end = (uint32_t)g_text.size;
            for (int i = saved_n_break; i < g_func.n_break_fixups; i++)
                patch_jal(g_func.break_fixups[i], (int32_t)end - (int32_t)g_func.break_fixups[i]);
            g_func.n_break_fixups = saved_n_break;
            g_func.break_label = saved_break;
            g_func.switch_depth--;
            parse_expect(T_RBRACE, "expected '}' after switch");
            return;
        }
        default: {
            /* Expression statement. */
            val_t v = parse_expr(lx);
            (void)v;
            parse_expect(T_SEMI, "expected ';'");
            return;
        }
    }
}

static void parse_stmt(lexer_t *lx) {
    lexer_t *saved_g_lx = g_lx;
    g_lx = lx;
    parse_stmt_body(lx);
    g_lx = saved_g_lx;
}

static void parse_compound(lexer_t *lx) {
    parse_expect(T_LBRACE, "expected '{'");
    symtab_push_scope();
    while (lx->cur.kind != T_RBRACE && lx->cur.kind != T_EOF) {
        parse_stmt(lx);
    }
    symtab_pop_scope();
    parse_expect(T_RBRACE, "expected '}'");
}

/* ---- Constant expression parser (for array sizes, etc.) ------------ */

static int const_op_prec(token_kind_t k) {
    switch (k) {
        case T_OR: return 1;
        case T_AND: return 2;
        case T_PIPE: return 3;
        case T_CARET: return 4;
        case T_AMP: return 5;
        case T_EQ: case T_NE: return 6;
        case T_LT: case T_GT: case T_LE: case T_GE: return 7;
        case T_SHL: case T_SHR: return 8;
        case T_PLUS: case T_MINUS: return 9;
        case T_STAR: case T_SLASH: case T_PERCENT: return 10;
        default: return 0;
    }
}

static long long const_expr_prim(lexer_t *lx);
static long long const_expr_binop(lexer_t *lx, int min_prec);

static long long const_expr_unary(lexer_t *lx) {
    if (lx->cur.kind == T_PLUS) { lex_next(lx); return const_expr_unary(lx); }
    if (lx->cur.kind == T_MINUS) { lex_next(lx); return -const_expr_unary(lx); }
    if (lx->cur.kind == T_NOT) { lex_next(lx); return !const_expr_unary(lx); }
    if (lx->cur.kind == T_TILDE) { lex_next(lx); return ~const_expr_unary(lx); }
    if (lx->cur.kind == T_LPAREN) {
        lex_next(lx);
        long long v = const_expr_binop(lx, 0);
        if (lx->cur.kind == T_RPAREN) lex_next(lx);
        return v;
    }
    if (lx->cur.kind == T_INT || lx->cur.kind == T_LONG ||
        lx->cur.kind == T_UINT || lx->cur.kind == T_ULONG) {
        long long v = (long long)lx->cur.ival;
        lex_next(lx);
        return v;
    }
    if (lx->cur.kind == T_IDENT) {
        int idx = symtab_lookup_global(lx->cur.text);
        lex_next(lx);
        if (idx >= 0 && g_globals[idx].kind == SYM_ENUM_CONST)
            return (long long)g_globals[idx].enum_val;
        return 0;
    }
    cc_error("expected constant expression");
    return 0;
}

static long long const_expr_binop(lexer_t *lx, int min_prec) {
    long long lhs = const_expr_unary(lx);
    for (;;) {
        int prec = const_op_prec(lx->cur.kind);
        if (prec < min_prec || prec == 0) break;
        token_kind_t op = lx->cur.kind;
        lex_next(lx);
        long long rhs = const_expr_binop(lx, prec + 1);
        switch (op) {
            case T_PLUS: lhs = lhs + rhs; break;
            case T_MINUS: lhs = lhs - rhs; break;
            case T_STAR: lhs = lhs * rhs; break;
            case T_SLASH: lhs = rhs ? lhs / rhs : 0; break;
            case T_PERCENT: lhs = rhs ? lhs % rhs : 0; break;
            case T_SHL: lhs = lhs << rhs; break;
            case T_SHR: lhs = lhs >> rhs; break;
            case T_AMP: lhs = lhs & rhs; break;
            case T_PIPE: lhs = lhs | rhs; break;
            case T_CARET: lhs = lhs ^ rhs; break;
            case T_AND: lhs = lhs && rhs; break;
            case T_OR: lhs = lhs || rhs; break;
            case T_EQ: lhs = (lhs == rhs); break;
            case T_NE: lhs = (lhs != rhs); break;
            case T_LT: lhs = (lhs < rhs); break;
            case T_GT: lhs = (lhs > rhs); break;
            case T_LE: lhs = (lhs <= rhs); break;
            case T_GE: lhs = (lhs >= rhs); break;
            default: break;
        }
    }
    return lhs;
}

static long long gen_const_expr(lexer_t *lx) {
    return const_expr_binop(lx, 0);
}

/* ---- Global init parser (constant only) ---------------------------- */
expr_t *gen_parse_global_init(lexer_t *lx, type_t *type) {
    /* Support string literal initializers: char *s = "hello"; char s[] = "hello" */
    if (lx->cur.kind == T_STRING && type->kind != TY_STRUCT && type->kind != TY_UNION &&
        !(type->kind == TY_ARRAY && (type->base->kind == TY_STRUCT || type->base->kind == TY_UNION))) {
        /* Add string to rodata pool. */
        size_t slen;
        char *s = parse_concat_string(lx, &slen);
        uint32_t off = cc_strpool_add(s, slen);
        /* Create an EX_STR node; ival holds the rodata offset, str_len the byte count. */
        expr_t *e = ast_new_expr(EX_STR, lx->cur.pos);
        e->ival = off;
        e->str_len = slen;
        free(s);
        return e;
    }

    /* Handle brace-enclosed initializer lists: { ... } */
    if (lx->cur.kind == T_LBRACE) {
        uint32_t start_off = (uint32_t)g_data.size;
        gen_handle_braced_init_global(lx, type);

        /* For incomplete arrays, compute element count from bytes written. */
        if (type->kind == TY_ARRAY && (type->length == 0 || type->is_incomplete)) {
            uint64_t elem_size = type_sizeof(type->base);
            uint64_t total_bytes = g_data.size - start_off;
            uint64_t n_elems = elem_size > 0 ? (total_bytes / elem_size) : 0;
            type->length = n_elems;
            type->is_incomplete = false;
            type->size = total_bytes;
            type->is_complete = true;
        }

        expr_t *e = ast_new_expr(EX_INIT_LIST, lx->cur.pos);
        e->ival = start_off;
        e->type = type;
        return e;
    }

    /* Address-of-global initializers: &symbol, or a bare array/function
     * name that decays to its address. Resolved at finalize time. */
    {
        bool handled = false;
        if (lx->cur.kind == T_AMP) {
            lexer_t save = *lx;
            lex_next(lx);
            if (lx->cur.kind == T_IDENT) {
                int idx = symtab_lookup_global(lx->cur.text);
                if (idx < 0 && g_opts.n_input_files > 1) {
                    char mangled[CC_MAX_IDENT + 16];
                    mangle_static_name(lx->cur.text, mangled, sizeof(mangled));
                    idx = symtab_lookup_global(mangled);
                }
                if (idx >= 0 && (g_globals[idx].kind == SYM_GLOBAL_VAR ||
                                 g_globals[idx].kind == SYM_FUNCTION)) {
                    lex_next(lx);
                    expr_t *e = ast_new_expr(EX_ADDR, lx->cur.pos);
                    e->ival = idx;
                    e->type = type_make_ptr(g_globals[idx].type);
                    return e;
                }
            }
            *lx = save;   /* not a resolvable symbol — fall through */
            handled = true;
        }
        if (!handled && lx->cur.kind == T_IDENT) {
            lexer_t save = *lx;
            char nm[CC_MAX_IDENT];
            strncpy(nm, lx->cur.text, CC_MAX_IDENT - 1);
            nm[CC_MAX_IDENT - 1] = 0;
            lex_next(lx);
            if (lx->cur.kind == T_COMMA || lx->cur.kind == T_SEMI) {
                int idx = symtab_lookup_global(nm);
                if (idx < 0 && g_opts.n_input_files > 1) {
                    char mangled[CC_MAX_IDENT + 16];
                    mangle_static_name(nm, mangled, sizeof(mangled));
                    idx = symtab_lookup_global(mangled);
                }
                if (idx >= 0 && g_globals[idx].kind == SYM_GLOBAL_VAR &&
                    (g_globals[idx].type->kind == TY_ARRAY)) {
                    /* Array name decays to its address. */
                    expr_t *e = ast_new_expr(EX_ADDR, lx->cur.pos);
                    e->ival = idx;
                    e->type = type_make_ptr(g_globals[idx].type->base);
                    return e;
                }
                if (idx >= 0 && g_globals[idx].kind == SYM_FUNCTION) {
                    /* Function name decays to pointer-to-function. */
                    expr_t *e = ast_new_expr(EX_ADDR, lx->cur.pos);
                    e->ival = idx;
                    e->type = type_make_ptr(g_globals[idx].type);
                    return e;
                }
            }
            *lx = save;
        }
    }

    /* For non-string: support only integer/pointer constants. */
    val_t v = parse_assign(lx);
    expr_t *e = ast_new_expr(EX_NUM, lx->cur.pos);
    if (v.kind == VAL_IMM) {
        e->ival = v.imm;
    } else {
        cc_error("non-constant global initializer");
        e->ival = 0;
    }
    return e;
}

/* ---- Brace-elided initializer processing (global) --------------------- */
/* Process a sub-aggregate initializer whose braces are elided.
 * Unlike gen_handle_braced_init_global(), this function does NOT consume
 * a leading '{' or expect a trailing '}'.  It reads scalar/string values
 * and recurses for nested aggregates from the current token stream.
 * Returns the total number of bytes written to g_data for this level. */
static uint64_t gen_emit_global_init_elided(lexer_t *lx, type_t *type) {
    uint64_t start_off = (uint64_t)g_data.size;

    if (type->kind == TY_ARRAY) {
        type_t *elem = type->base;
        uint64_t elem_sz = type_sizeof(elem);
        uint64_t max = type->length > 0 ? type->length : UINT64_MAX;
        bool first = true;

        /* String literal initializes the whole char array at once,
         * not element-by-element. */
        if (lx->cur.kind == T_STRING &&
            (elem->kind == TY_CHAR || elem->kind == TY_SCHAR || elem->kind == TY_UCHAR)) {
            gen_emit_global_init_scalar(lx, type);
            return (uint64_t)(g_data.size - start_off);
        }

        for (uint64_t i = 0; i < max; i++) {
            if (lx->cur.kind == T_RBRACE || lx->cur.kind == T_EOF) break;
            if (!first) {
                if (lx->cur.kind != T_COMMA) break;
                lex_next(lx);
                if (lx->cur.kind == T_RBRACE) break;
            }
            first = false;

            if ((elem->kind == TY_ARRAY || elem->kind == TY_STRUCT || elem->kind == TY_UNION) &&
                lx->cur.kind == T_LBRACE) {
                gen_handle_braced_init_global(lx, elem);
            } else if (elem->kind == TY_ARRAY || elem->kind == TY_STRUCT || elem->kind == TY_UNION) {
                gen_emit_global_init_elided(lx, elem);
            } else {
                gen_emit_global_init_scalar(lx, elem);
            }
        }

        /* Pad remaining elements with zeros if array size is fixed. */
        if (type->length > 0 && !type->is_incomplete) {
            uint64_t total = type->length * elem_sz;
            while ((uint64_t)(g_data.size - start_off) < total) {
                cc_buf_push8(&g_data, 0);
            }
        }

    } else if (type->kind == TY_STRUCT || type->kind == TY_UNION) {
        bool first = true;
        for (int i = 0; i < type->nfields; i++) {
            struct_field_t *f = &type->fields[i];
            if (f->is_anon) continue;

            if (!first) {
                if (lx->cur.kind != T_COMMA) break;
                lex_next(lx);
                if (lx->cur.kind == T_RBRACE) break;
            }
            first = false;

            /* Emit padding to reach field offset. */
            while ((uint64_t)(g_data.size - start_off) < f->offset) {
                cc_buf_push8(&g_data, 0);
            }

            if ((f->type->kind == TY_ARRAY || f->type->kind == TY_STRUCT || f->type->kind == TY_UNION) &&
                lx->cur.kind == T_LBRACE) {
                gen_handle_braced_init_global(lx, f->type);
            } else if (f->type->kind == TY_ARRAY || f->type->kind == TY_STRUCT || f->type->kind == TY_UNION) {
                gen_emit_global_init_elided(lx, f->type);
            } else {
                gen_emit_global_init_scalar(lx, f->type);
            }
        }

        /* Fill to struct/union total size. */
        uint64_t total = type_sizeof(type);
        while ((uint64_t)(g_data.size - start_off) < total) {
            cc_buf_push8(&g_data, 0);
        }

    } else {
        gen_emit_global_init_scalar(lx, type);
    }

    return (uint64_t)(g_data.size - start_off);
}

/* ---- Brace initializer list parsing (global) ------------------------- */
/* Recursively parse a brace-enclosed initializer list.
 * Writes bytes directly to g_data.  Handles:
 *   - Array initializers: int arr[] = {1,2,3};
 *   - Struct initializers: struct S s = {field1, field2};
 *   - Nested braces for aggregates-in-aggregates.
 *   - String literals for pointer/char-array fields.
 *   - Brace-elided sub-aggregate initializers.
 * Returns the total number of bytes written to g_data for this level. */
static uint64_t gen_handle_braced_init_global(lexer_t *lx, type_t *type) {
    lex_next(lx); /* consume '{' */
    uint64_t start_off = (uint64_t)g_data.size;
    uint64_t elem_idx = 0;
    bool first = true;

    if (type->kind == TY_ARRAY) {
        type_t *elem = type->base;
        uint64_t elem_sz = type_sizeof(elem);
        uint64_t max = type->length > 0 ? type->length : UINT64_MAX;

        while (lx->cur.kind != T_RBRACE && lx->cur.kind != T_EOF && elem_idx < max) {
            if (!first) {
                if (!accept(T_COMMA)) break;
                if (lx->cur.kind == T_RBRACE) break;
            }
            first = false;

            /* If element is aggregate and next token is '{', recurse */
            if ((elem->kind == TY_ARRAY || elem->kind == TY_STRUCT || elem->kind == TY_UNION) &&
                lx->cur.kind == T_LBRACE) {
                gen_handle_braced_init_global(lx, elem);
            } else if (elem->kind == TY_ARRAY || elem->kind == TY_STRUCT || elem->kind == TY_UNION) {
                /* Brace-elided sub-aggregate initializer. */
                gen_emit_global_init_elided(lx, elem);
            } else {
                gen_emit_global_init_scalar(lx, elem);
            }
            elem_idx++;
        }

        /* Pad remaining elements with zeros if array size is fixed. */
        if (type->length > 0 && !type->is_incomplete) {
            uint64_t total = type->length * elem_sz;
            while ((uint64_t)(g_data.size - start_off) < total) {
                cc_buf_push8(&g_data, 0);
            }
        }

    } else if (type->kind == TY_STRUCT || type->kind == TY_UNION) {
        for (int i = 0; i < type->nfields; i++) {
            struct_field_t *f = &type->fields[i];
            if (f->is_anon) continue; /* skip anonymous fields for now */

            if (!first) {
                if (!accept(T_COMMA)) break;
                if (lx->cur.kind == T_RBRACE) break;
            }
            first = false;

            /* Emit padding to reach field offset. */
            while ((uint64_t)(g_data.size - start_off) < f->offset) {
                cc_buf_push8(&g_data, 0);
            }

            if ((f->type->kind == TY_ARRAY || f->type->kind == TY_STRUCT || f->type->kind == TY_UNION) &&
                lx->cur.kind == T_LBRACE) {
                gen_handle_braced_init_global(lx, f->type);
            } else if (f->type->kind == TY_ARRAY || f->type->kind == TY_STRUCT || f->type->kind == TY_UNION) {
                gen_emit_global_init_elided(lx, f->type);
            } else {
                gen_emit_global_init_scalar(lx, f->type);
            }
        }

        /* Fill to struct/union total size. */
        uint64_t total = type_sizeof(type);
        while ((uint64_t)(g_data.size - start_off) < total) {
            cc_buf_push8(&g_data, 0);
        }

    } else {
        /* Scalar in braces: int x = {42}; */
        gen_emit_global_init_scalar(lx, type);
    }

    /* Allow a trailing comma before the closing brace: {1, 2, 3,}. */
    accept(T_COMMA);
    parse_expect(T_RBRACE, "expected '}'");
    return (uint64_t)(g_data.size - start_off);
}

/* ---- Brace element counting (for incomplete array sizing) ------------ */
/* Count the number of top-level elements in a brace-enclosed initializer.
 * This is a pure scan: the lexer position is saved and restored. */
static uint64_t gen_count_brace_elements(lexer_t *lx, type_t *type) {
    size_t saved_pos = lx->pos;
    int saved_line = lx->line;
    int saved_col = lx->col;
    token_t saved_cur = lx->cur;
    token_t saved_next = lx->next;
    bool saved_has_next = lx->has_next;

    uint64_t count = 0;
    bool first = true;

    if (lx->cur.kind != T_LBRACE) {
        return 0;
    }
    lex_next(lx); /* consume '{' */

    while (lx->cur.kind != T_RBRACE && lx->cur.kind != T_EOF) {
        if (!first) {
            if (lx->cur.kind != T_COMMA) break;
            lex_next(lx);
            if (lx->cur.kind == T_RBRACE) break;
        }
        first = false;

        /* Skip one element, handling nested parentheses/braces/brackets.
         * String and char literals are single tokens, so no special care needed. */
        int depth = 0;
        while (lx->cur.kind != T_EOF) {
            if (lx->cur.kind == T_LPAREN || lx->cur.kind == T_LBRACKET || lx->cur.kind == T_LBRACE) {
                depth++;
            } else if (lx->cur.kind == T_RPAREN || lx->cur.kind == T_RBRACKET || lx->cur.kind == T_RBRACE) {
                if (depth == 0) break;
                depth--;
            } else if (lx->cur.kind == T_COMMA && depth == 0) {
                break;
            }
            lex_next(lx);
        }
        count++;
    }

    lx->pos = saved_pos;
    lx->line = saved_line;
    lx->col = saved_col;
    lx->cur = saved_cur;
    lx->next = saved_next;
    lx->has_next = saved_has_next;
    return count;
}

/* ---- Local brace initializer ----------------------------------------- */
/* Emit store instructions for a brace-enclosed initializer of a local variable.
 * `fp_off` is the fp-relative stack offset of the variable.
 * `sym_idx` is the local symbol index. */
static void gen_emit_local_braced_init(lexer_t *lx, type_t *type,
                                       int64_t fp_off, int sym_idx);
static void gen_emit_local_init_recurse(lexer_t *lx, type_t *type,
                                        int64_t fp_off, int64_t field_off);

static void gen_emit_local_init_scalar(lexer_t *lx, type_t *type,
                                       int64_t fp_off, int64_t field_off) {
    if (lx->cur.kind == T_STRING) {
        size_t slen;
        char *s = parse_concat_string(lx, &slen);
        uint32_t rodata_off = cc_strpool_add(s, slen);
        free(s);
        /* Load string address into T0 via lui+addi with rodata fixup. */
        uint32_t lui_off = (uint32_t)g_text.size;
        rv_lui(RV_T0, 0);
        uint32_t addi_off = (uint32_t)g_text.size;
        rv_addi(RV_T0, RV_T0, 0);
        if (g_n_rodata_fixups >= 4096) cc_fatal("too many rodata fixups");
        g_rodata_fixups[g_n_rodata_fixups].rodata_off = rodata_off;
        g_rodata_fixups[g_n_rodata_fixups].patch_lui = lui_off;
        g_rodata_fixups[g_n_rodata_fixups].patch_addi = addi_off;
        g_n_rodata_fixups++;

        /* Store the pointer to the stack slot. */
        rv_sd(RV_T0, RV_FP, (int)(fp_off + field_off));
        return;
    }

    /* Scalar: evaluate expression and store to stack slot. */
    val_t v = parse_assign(lx);
    int addr_reg = RV_T2;
    load_value(&v, RV_T0);
    uint64_t sz = type_sizeof(type);
    /* Compute address: fp + fp_off + field_off. */
    int total_off = (int)(fp_off + field_off);
    rv_addi(addr_reg, RV_FP, total_off);
    switch (sz) {
        case 1: rv_sb(RV_T0, addr_reg, 0); break;
        case 2: rv_sh(RV_T0, addr_reg, 0); break;
        case 4: rv_sw(RV_T0, addr_reg, 0); break;
        case 8: rv_sd(RV_T0, addr_reg, 0); break;
        default: rv_sd(RV_T0, addr_reg, 0); break;
    }
}

static void gen_emit_local_init_recurse(lexer_t *lx, type_t *type,
                                        int64_t fp_off, int64_t field_off) {
    if (type->kind == TY_ARRAY) {
        type_t *elem = type->base;
        uint64_t elem_sz = type_sizeof(elem);
        uint64_t max = type->length > 0 ? type->length : UINT64_MAX;
        uint64_t count = 0;
        bool first_elem = true;

        while (lx->cur.kind != T_RBRACE && lx->cur.kind != T_EOF && count < max) {
            if (!first_elem) {
                if (lx->cur.kind != T_COMMA) break;
                lex_next(lx);
                if (lx->cur.kind == T_RBRACE) break;
            }
            first_elem = false;

            if ((elem->kind == TY_ARRAY || elem->kind == TY_STRUCT || elem->kind == TY_UNION) &&
                lx->cur.kind == T_LBRACE) {
                lex_next(lx);
                gen_emit_local_init_recurse(lx, elem, fp_off, field_off + (int64_t)(count * elem_sz));
                if (lx->cur.kind == T_RBRACE) lex_next(lx);
                else parse_error("expected '}'");
            } else if (elem->kind == TY_ARRAY || elem->kind == TY_STRUCT || elem->kind == TY_UNION) {
                gen_emit_local_init_recurse(lx, elem, fp_off, field_off + (int64_t)(count * elem_sz));
            } else {
                gen_emit_local_init_scalar(lx, elem, fp_off, field_off + (int64_t)(count * elem_sz));
            }
            count++;
        }
    } else if (type->kind == TY_STRUCT || type->kind == TY_UNION) {
        bool first_field = true;
        for (int i = 0; i < type->nfields; i++) {
            struct_field_t *f = &type->fields[i];
            if (f->is_anon) continue;
            if (!first_field) {
                if (lx->cur.kind != T_COMMA) break;
                lex_next(lx);
                if (lx->cur.kind == T_RBRACE) break;
            }
            first_field = false;

            if ((f->type->kind == TY_ARRAY || f->type->kind == TY_STRUCT || f->type->kind == TY_UNION) &&
                lx->cur.kind == T_LBRACE) {
                lex_next(lx);
                gen_emit_local_init_recurse(lx, f->type, fp_off,
                                           field_off + (int64_t)f->offset);
                if (lx->cur.kind == T_RBRACE) lex_next(lx);
                else parse_error("expected '}'");
            } else if (f->type->kind == TY_ARRAY || f->type->kind == TY_STRUCT || f->type->kind == TY_UNION) {
                gen_emit_local_init_recurse(lx, f->type, fp_off,
                                           field_off + (int64_t)f->offset);
            } else {
                gen_emit_local_init_scalar(lx, f->type, fp_off,
                                          field_off + (int64_t)f->offset);
            }
        }
    } else {
        /* Scalar in braces: int x = {42}; */
        gen_emit_local_init_scalar(lx, type, fp_off, field_off);
    }
}

static void gen_emit_local_braced_init(lexer_t *lx, type_t *type,
                                       int64_t fp_off, int sym_idx) {
    (void)sym_idx;
    lex_next(lx); /* consume '{' */
    gen_emit_local_init_recurse(lx, type, fp_off, 0);
    if (lx->cur.kind == T_COMMA) lex_next(lx); /* optional trailing comma */
    if (lx->cur.kind == T_RBRACE) lex_next(lx);
    else parse_error("expected '}'");
}

/* Emit a single scalar (or string) constant initializer for global scope.
 * Writes the value bytes to g_data. */
static void gen_emit_global_init_scalar(lexer_t *lx, type_t *type) {
    if (lx->cur.kind == T_STRING) {
        size_t raw_len;
        char *s = parse_concat_string(lx, &raw_len);
        uint32_t rodata_off = cc_strpool_add(s, raw_len);
        size_t slen = raw_len + 1; /* include NUL */

        if (type->kind == TY_PTR) {
            /* char *field = "str" — write placeholder pointer, record fixup. */
            uint32_t data_off = (uint32_t)g_data.size;
            cc_buf_push64(&g_data, 0);
            if (g_n_data_rodata_fixups >= 4096)
                cc_fatal("too many data-rodata fixups");
            g_data_rodata_fixups[g_n_data_rodata_fixups].rodata_off = rodata_off;
            g_data_rodata_fixups[g_n_data_rodata_fixups].data_off = data_off;
            g_n_data_rodata_fixups++;
        } else if (type->kind == TY_ARRAY && type->base &&
                   (type->base->kind == TY_CHAR || type->base->kind == TY_SCHAR || type->base->kind == TY_UCHAR)) {
            /* char s[] = "str" — copy bytes. */
            cc_buf_push(&g_data, g_rodata.data + rodata_off, slen);
            /* Pad to array size if fixed. */
            if (type->length > 0 && !type->is_incomplete) {
                uint64_t sz = type_sizeof(type);
                while (slen < sz) { cc_buf_push8(&g_data, 0); slen++; }
            }
        } else {
            cc_error("string initializer for non-pointer/non-array type");
        }
        free(s);
        return;
    }

    /* Scalar integer/pointer value. */
    val_t v = parse_assign(lx);
    uint64_t sz = type_sizeof(type);
    uint64_t val = 0;
    if (v.kind == VAL_IMM) {
        val = v.imm;
    } else {
        cc_error("non-constant initializer element");
    }
    for (uint64_t i = 0; i < sz; i++) {
        cc_buf_push8(&g_data, (uint8_t)(val >> (8 * i)));
    }
}

/* ---- Function body codegen ----------------------------------------- */
static char g_cur_func_name[CC_MAX_IDENT] = "";

static void gen_func_body(lexer_t *lx, decl_t *d) {
    type_t *ftype = d->type;
    symtab_enter_function(ftype);
    strncpy(g_cur_func_name, d->name, CC_MAX_IDENT - 1);
    g_cur_func_name[CC_MAX_IDENT - 1] = 0;
    g_cur_func_ret = ftype->ret;

    /* Frame bookkeeping. cur_offset starts at 16: the first local lands
     * at fp-16; saved ra/fp live at the very BOTTOM of the frame
     * (fp-frame+8, fp-frame+0), patched in once the size is known. */
    g_func.frame_size = 16;
    g_func.cur_offset = 16;
    g_func.max_call_args = 0;
    g_func.loop_depth = 0;
    g_func.n_break_fixups = 0;
    g_func.n_continue_fixups = 0;
    g_func.in_function = true;
    g_func.nparams = ftype->nparams;
    g_func.is_variadic = ftype->is_varargs;
    g_func.n_va_locals = 0;
    g_func.n_struct_locals = 0;
    label_clear();
    g_n_epilogue_patches = 0;

    /* Pre-allocate slots for parameters. */
    int arg_regs[8] = {RV_A0, RV_A1, RV_A2, RV_A3, RV_A4, RV_A5, RV_A6, RV_A7};
    int *param_slots = (int *)calloc(ftype->nparams, sizeof(int));
    for (int i = 0; i < ftype->nparams; i++) {
        type_t *ptyp = ftype->params[i].type;
        uint64_t sz = type_sizeof(ptyp);
        uint64_t al = type_alignof(ptyp);
        g_func.cur_offset += sz;
        g_func.cur_offset = (g_func.cur_offset + al - 1) & ~(al - 1);
        int64_t off = -(int64_t)g_func.cur_offset;
        int idx = symtab_install_local(ftype->params[i].name, SYM_LOCAL_VAR, ptyp, 1);
        g_locals[idx].offset = off;
        param_slots[i] = (int)off;
    }
    if (g_func.cur_offset > g_func.frame_size) g_func.frame_size = g_func.cur_offset;

    /* For variadic functions: reserve 64 bytes for register save area (a0-a7). */
    if (ftype->is_varargs) {
        g_func.cur_offset += 64;
        g_func.cur_offset = (g_func.cur_offset + 7) & ~7;
    }
    if (g_func.cur_offset > g_func.frame_size) g_func.frame_size = g_func.cur_offset;

    /* Parse body — but we need to know frame_size BEFORE prologue.
     * For MVP we use a two-pass approach: parse into AST statements,
     * then emit prologue + body. Simpler: emit prologue with conservative
     * frame size = current cur_offset + space for max_call_args.
     * But we don't know max_call_args yet. So emit prologue AFTER parsing.
     *
     * Approach: parse body into g_text with a placeholder prologue,
     * then patch frame_size. */
    uint32_t prologue_start = (uint32_t)g_text.size;
    /* t1 = frame (patched lui+addi pair; a single addi immediate cannot
     * represent +2048 or larger, which silently became NEGATIVE and put
     * fp BELOW sp for functions with big locals like vformat_run). */
    uint32_t prologue_lui_off = (uint32_t)g_text.size;
    rv_lui(RV_T1, 0);
    uint32_t prologue_addi_off = (uint32_t)g_text.size;
    rv_addi(RV_T1, RV_T1, 0);
    rv_sub(RV_SP, RV_SP, RV_T1);    /* sp -= frame */
    /* sd ra, 8(sp) — ra at frame bottom + 8 */
    rv_sd(RV_RA, RV_SP, 8);
    /* sd fp, 0(sp) — old fp at frame bottom */
    rv_sd(RV_FP, RV_SP, 0);
    /* fp = sp + frame = TOP of frame (caller's sp).
     * Locals live at fp-N, INSIDE the frame. */
    rv_add(RV_FP, RV_SP, RV_T1);

    /* Store parameters from a0-a7 / fa0-fa7 to their slots.
     * Integer params arrive in the a-regs; float/double params arrive in
     * the fa-regs (independent register files — matching gen_call).
     * The old code stored EVERY param from the a-regs, so any function
     * taking a double read garbage (sqrt(4.0) computed from junk). */
    {
        int gp_i = 0, fp_i = 0;
        for (int i = 0; i < ftype->nparams; i++) {
            int off = param_slots[i];
            type_t *ptyp = ftype->params[i].type;
            bool is_fp = is_float_type(ptyp);
            bool is_struct = (ptyp->kind == TY_STRUCT || ptyp->kind == TY_UNION);
            if (is_fp) {
                if (fp_i < 8) {
                    rv_fsd(RV_FA0 + fp_i, RV_FP, off);
                    fp_i++;
                }
            } else if (is_struct) {
                /* Struct passed BY VALUE: the register holds a POINTER to
                 * the caller's copy — dereference into the local slot so
                 * p.field accesses read the struct's bytes directly. */
                if (gp_i < 8) {
                    uint64_t sz = type_sizeof(ptyp);
                    if (sz > 32) sz = 32;
                    rv_addi(RV_T0, RV_FP, off);       /* &local slot */
                    for (uint64_t k = 0; k < sz; k += 8) {
                        rv_ld(RV_T1, arg_regs[gp_i], (int)k);
                        rv_sd(RV_T1, RV_T0, (int)k);
                    }
                    gp_i++;
                }
            } else {
                if (gp_i < 8) {
                    uint64_t sz = type_sizeof(ptyp);
                    switch (sz) {
                        case 1: rv_sb(arg_regs[gp_i], RV_FP, off); break;
                        case 2: rv_sh(arg_regs[gp_i], RV_FP, off); break;
                        case 4: rv_sw(arg_regs[gp_i], RV_FP, off); break;
                        default: rv_sd(arg_regs[gp_i], RV_FP, off); break;
                    }
                    gp_i++;
                }
            }
        }
    }

    /* For variadic functions: save all a0-a7 to the register save area.
     * The 64-byte area MUST be allocated inside the frame (below every
     * local). The old code neither reserved the space nor aligned it, so
     * with fp at the frame top the a5-a7 stores landed ABOVE fp — inside
     * the CALLER's frame, silently corrupting its locals (vfprintf's ap
     * became garbage). */
    if (ftype->is_varargs) {
        /* GP save area (a0-a7). */
        g_func.cur_offset += 64;
        g_func.cur_offset = (g_func.cur_offset + 15) & ~15;
        g_func.va_save_off = -(int)g_func.cur_offset;
        if (g_func.cur_offset > g_func.frame_size) {
            g_func.frame_size = g_func.cur_offset;
        }
        for (int i = 0; i < 8; i++) {
            rv_sd(arg_regs[i], RV_FP, g_func.va_save_off + i * 8);
        }
        /* FP save area (fa0-fa7): doubles in varargs are passed in the
         * FP registers — without saving them, va_arg(ap, double) read
         * garbage from the GP area. */
        g_func.cur_offset += 64;
        g_func.cur_offset = (g_func.cur_offset + 15) & ~15;
        g_func.va_fsave_off = -(int)g_func.cur_offset;
        if (g_func.cur_offset > g_func.frame_size) {
            g_func.frame_size = g_func.cur_offset;
        }
        for (int i = 0; i < 8; i++) {
            rv_fsd(RV_FA0 + i, RV_FP, g_func.va_fsave_off + i * 8);
        }
    }

    parse_compound(lx);

    /* Default return. */
    emit_function_epilogue();

    /* Patch prologue and epilogues with the real frame size, using the
     * standard lui/addi decomposition: lui gets (frame+0x800)>>12, addi
     * gets frame - (lui<<12) (always fits in [-2048, 2047]). */
    int frame = (g_func.frame_size + 15) & ~15;
    /* The frame must ALSO hold saved ra/fp BELOW the deepest local. */
    frame = (frame + 16) & ~15;
    {
        uint32_t lui_imm = (uint32_t)(((uint64_t)frame + 0x800) >> 12);
        int32_t addi_imm = (int32_t)frame - (int32_t)(lui_imm << 12);

        /* Prologue lui+addi (t1 = frame). */
        for (int k = 0; k < 2; k++) {
            uint32_t off = k == 0 ? prologue_lui_off : prologue_addi_off;
            uint8_t *p = g_text.data + off;
            uint32_t insn = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
            if (k == 0) {
                insn &= ~0xFFFFF000u;
                insn |= (lui_imm & 0xFFFFF) << 12;
            } else {
                insn &= ~0xFFF00000u;
                insn |= ((uint32_t)addi_imm & 0xFFF) << 20;
            }
            p[0] = insn & 0xff; p[1] = (insn >> 8) & 0xff;
            p[2] = (insn >> 16) & 0xff; p[3] = (insn >> 24) & 0xff;
        }

        /* Epilogue lui+addi pairs. */
        for (int i = 0; i < g_n_epilogue_patches; i++) {
            int kind = g_epilogue_patch_neg[i];
            uint8_t *q = g_text.data + g_epilogue_patch_off[i];
            uint32_t e = (uint32_t)q[0] | ((uint32_t)q[1] << 8) |
                         ((uint32_t)q[2] << 16) | ((uint32_t)q[3] << 24);
            if (kind == 2) {
                e &= ~0xFFFFF000u;
                e |= (lui_imm & 0xFFFFF) << 12;
            } else if (kind == 3) {
                e &= ~0xFFF00000u;
                e |= ((uint32_t)addi_imm & 0xFFF) << 20;
            }
            q[0] = e & 0xff; q[1] = (e >> 8) & 0xff;
            q[2] = (e >> 16) & 0xff; q[3] = (e >> 24) & 0xff;
        }
    }
    g_n_epilogue_patches = 0;

    rv_resolve_fixups();
    label_check_unresolved();
    free(param_slots);

    g_func.in_function = false;
    symtab_leave_function();
}

/* ---- Top-level declaration codegen --------------------------------- */
void gen_init(void) {
    cc_buf_init(&g_text);
    cc_buf_init(&g_rodata);
    cc_buf_init(&g_data);
    g_bss_size = 0;
    g_entry = 0;

    /* Built-in typedefs recognized by the compiler:
     * __builtin_va_list — va_list is a plain char* on our ABI (the
     * va_start/va_arg builtins treat it as a pointer). Without this,
     * `va_list` params resolved to implicit int (4 bytes), corrupting
     * variadic frames. */
    /* va_list = char* — points to a 16-byte cursor pair:
     *   [ap+0] = GP cursor (integer varargs, walks the a0-a7 save area)
     *   [ap+8] = FP cursor (double varargs, walks the fa0-fa7 save area)
     * va_start allocates the pair in the frame and stores both cursors. */
    symtab_install_global("__builtin_va_list", SYM_TYPEDEF, type_make_ptr(&ty_char));
}

void gen_reset_for_file(void) {
    memset(&g_func, 0, sizeof(g_func));
    label_clear();
    symtab_reset_locals();
    cc_arena_reset(&g_ast_arena);
}

/* Helper: mangle a static symbol name with per-file prefix. */
static void mangle_static_name(const char *name, char *out, size_t out_sz) {
    snprintf(out, out_sz, "__f%d_%s", g_opts.current_file_idx, name);
}

void gen_decl(decl_t *d) {
    char name[CC_MAX_IDENT];
    strncpy(name, d->name, CC_MAX_IDENT - 1);
    name[CC_MAX_IDENT - 1] = 0;

    /* Mangle static symbol names with per-file prefix to avoid collisions
     * when compiling multiple files into one binary. */
    if (d->type->is_static && g_opts.n_input_files > 1) {
        char mangled[CC_MAX_IDENT + 16];
        mangle_static_name(name, mangled, sizeof(mangled));
        strncpy(name, mangled, CC_MAX_IDENT - 1);
        name[CC_MAX_IDENT - 1] = 0;
    }
    switch (d->kind) {
        case D_TYPEDEF: {
            symtab_install_global(name, SYM_TYPEDEF, d->type);
            return;
        }
        case D_FUNC_DECL: {
            int idx = symtab_install_global(name, SYM_FUNCTION, d->type);
            g_globals[idx].is_defined = false;
            return;
        }
        case D_FUNC_DEF: {
            int idx = symtab_install_global(name, SYM_FUNCTION, d->type);
            const char *entry_name = g_opts.entry_sym ? g_opts.entry_sym : "_start";
            bool is_entry = (strcmp(name, entry_name) == 0);
            /* First definition of the entry symbol wins. Without this,
             * auto-linked libonyxc's _start (start_cc.c) silently replaced
             * a user program's own _start and hijacked the entry point. */
            if (is_entry && g_entry != 0) {
                cc_warn("'%s' defined twice (auto-linked libc?); keeping the first definition as entry", name);
                gen_func_body(g_lx, d);   /* still compile the body */
                return;
            }
            g_globals[idx].is_defined = true;
            g_globals[idx].text_off = (uint32_t)g_text.size;
            if (is_entry) {
                g_entry = CC_TEXT_VADDR + g_text.size;
            }
            gen_func_body(g_lx, d);
            return;
        }
        case D_VAR: {
            int idx = symtab_install_global(name, SYM_GLOBAL_VAR, d->type);
            /* Extern declarations without initializer are pure references —
             * don't allocate space; a definition elsewhere provides it. */
            if (d->type->is_extern && !d->init) {
                return;
            }
            if (d->init) {
                if (d->init->kind == EX_INIT_LIST) {
                    /* Brace initializer list — already written to g_data by
                     * gen_handle_braced_init_global().  Just record the offset. */
                    g_globals[idx].is_defined = true;
                    g_globals[idx].text_off = (uint32_t)d->init->ival;
                } else if (d->init->kind == EX_ADDR) {
                    /* Address-of-global initializer: &g_stdin_obj, array or
                     * function decay. Placeholder here; patched in
                     * gen_finalize once addresses are known. */
                    g_globals[idx].is_defined = true;
                    g_globals[idx].text_off = (uint32_t)g_data.size;
                    uint32_t data_off = (uint32_t)g_data.size;
                    cc_buf_push64(&g_data, 0);
                    if (g_n_data_sym_fixups >= 4096)
                        cc_fatal("too many data-symbol fixups");
                    g_data_sym_fixups[g_n_data_sym_fixups].sym_idx = (int)d->init->ival;
                    g_data_sym_fixups[g_n_data_sym_fixups].data_off = data_off;
                    g_n_data_sym_fixups++;
                } else if (d->init->kind == EX_NUM) {
                    /* Integer/pointer constant initializer. */
                    uint64_t v = d->init->ival;
                    g_globals[idx].is_defined = true;
                    g_globals[idx].text_off = (uint32_t)g_data.size;
                    uint64_t sz = type_sizeof(d->type);
                    for (uint64_t i = 0; i < sz; i++) {
                        cc_buf_push8(&g_data, (uint8_t)(v >> (8 * i)));
                    }
                } else if (d->init->kind == EX_STR) {
                    /* String literal initializer: char *s = "hello" or char s[] = "hello". */
                    uint32_t rodata_off = (uint32_t)d->init->ival;
                    g_globals[idx].is_defined = true;
                    g_globals[idx].text_off = (uint32_t)g_data.size;
                    if (d->type->kind == TY_PTR) {
                        /* char *s = "hello" → store the address of the string.
                         * Write placeholder 0; patched in gen_finalize with rodata address. */
                        uint32_t data_off = (uint32_t)g_data.size;
                        cc_buf_push64(&g_data, 0);
                        /* Record fixup so gen_finalize can patch the address. */
                        if (g_n_data_rodata_fixups >= 4096)
                            cc_fatal("too many data-rodata fixups");
                        g_data_rodata_fixups[g_n_data_rodata_fixups].rodata_off = rodata_off;
                        g_data_rodata_fixups[g_n_data_rodata_fixups].data_off = data_off;
                        g_n_data_rodata_fixups++;
                    } else if (d->type->kind == TY_ARRAY) {
                        /* char s[] = "hello" → copy string bytes (with NUL) into .data. */
                        size_t slen = d->init->str_len + 1; /* include NUL terminator */
                        cc_buf_push(&g_data, g_rodata.data + rodata_off, slen);
                        /* Pad to array size if specified and larger than string. */
                        if (d->type->length > 0 && !d->type->is_incomplete) {
                            uint64_t sz = type_sizeof(d->type);
                            while (slen < sz) {
                                cc_buf_push8(&g_data, 0);
                                slen++;
                            }
                        } else if (d->type->is_incomplete || d->type->length == 0) {
                            /* Incomplete array type (char s[] = "hello"): complete it
                             * from the string length so the symbol size is correct. */
                            d->type->length = slen;
                            d->type->is_incomplete = false;
                            d->type->is_complete = true;
                            d->type->size = slen;
                        }
                    } else {
                        cc_error("string initializer for non-pointer/non-array type");
                    }
                }
            } else {
                /* Uninitialized global → .bss. */
                g_globals[idx].text_off = (uint32_t)g_bss_size;
                g_bss_size += type_sizeof(d->type);
                g_bss_size = (g_bss_size + 7) & ~7;
            }
            return;
        }
        default:
            return;
    }
}

/* ---- Finalize ------------------------------------------------------- */
void gen_finalize(const char *entry_sym) {
    /* Assign virtual addresses to globals. */
    /* .text is at CC_TEXT_VADDR. .data starts after .text + .rodata. */
    uint64_t text_vaddr = CC_TEXT_VADDR;
    uint64_t rodata_vaddr = (text_vaddr + g_text.size + 7) & ~7ULL;
    uint64_t data_vaddr = (rodata_vaddr + g_rodata.size + 7) & ~7ULL;
    uint64_t bss_vaddr = (data_vaddr + g_data.size + 7) & ~7ULL;

    /* Patch all global symbol references. */
    for (int i = 0; i < g_n_globals; i++) {
        sym_t *s = &g_globals[i];
        if (s->kind == SYM_FUNCTION) {
            s->vaddr = text_vaddr + s->text_off;
        } else if (s->kind == SYM_GLOBAL_VAR) {
            /* If it had an initializer, it's in .data; otherwise .bss. */
            /* We need to know which — store a flag in is_defined. */
            if (s->is_defined) {
                s->vaddr = data_vaddr + s->text_off;
            } else {
                s->vaddr = bss_vaddr + s->text_off;
            }
        }
    }

    /* Patch all global address fixups (lui+addi sequences). */
    for (int i = 0; i < g_n_fixups; i++) {
        global_addr_fixup_t *f = &g_fixups[i];
        if (f->sym_idx < 0) continue;
        sym_t *s = &g_globals[f->sym_idx];
        uint64_t addr = s->vaddr + f->extra_off;
        /* Patch lui: imm20 = (addr + 0x800) >> 12. */
        int32_t lui_imm = (int32_t)((int64_t)(addr + 0x800) >> 12);
        int32_t lo = (int32_t)addr - (lui_imm << 12);
        uint8_t *p = g_text.data + f->patch_lui;
        uint32_t insn = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        insn &= ~0xFFFFF000;
        insn |= ((uint32_t)lui_imm & 0xFFFFF) << 12;
        p[0] = insn & 0xff; p[1] = (insn >> 8) & 0xff; p[2] = (insn >> 16) & 0xff; p[3] = (insn >> 24) & 0xff;
        if (f->patch_addi) {
            uint8_t *q = g_text.data + f->patch_addi;
            uint32_t i2 = (uint32_t)q[0] | ((uint32_t)q[1] << 8) | ((uint32_t)q[2] << 16) | ((uint32_t)q[3] << 24);
            i2 &= ~0xFFF00000;
            i2 |= ((uint32_t)(lo & 0xFFF)) << 20;
            q[0] = i2 & 0xff; q[1] = (i2 >> 8) & 0xff; q[2] = (i2 >> 16) & 0xff; q[3] = (i2 >> 24) & 0xff;
        }
    }

    /* Patch rodata fixups (string literals). */
    for (int i = 0; i < g_n_rodata_fixups; i++) {
        rodata_fixup_t *f = &g_rodata_fixups[i];
        uint64_t addr = rodata_vaddr + f->rodata_off;
        int32_t lui_imm = (int32_t)((int64_t)(addr + 0x800) >> 12);
        int32_t lo = (int32_t)addr - (lui_imm << 12);
        uint8_t *p = g_text.data + f->patch_lui;
        uint32_t insn = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        insn &= ~0xFFFFF000;
        insn |= ((uint32_t)lui_imm & 0xFFFFF) << 12;
        p[0] = insn & 0xff; p[1] = (insn >> 8) & 0xff; p[2] = (insn >> 16) & 0xff; p[3] = (insn >> 24) & 0xff;
        uint8_t *q = g_text.data + f->patch_addi;
        uint32_t i2 = (uint32_t)q[0] | ((uint32_t)q[1] << 8) | ((uint32_t)q[2] << 16) | ((uint32_t)q[3] << 24);
        i2 &= ~0xFFF00000;
        i2 |= ((uint32_t)(lo & 0xFFF)) << 20;
        q[0] = i2 & 0xff; q[1] = (i2 >> 8) & 0xff; q[2] = (i2 >> 16) & 0xff; q[3] = (i2 >> 24) & 0xff;
    }

    /* Patch data-rodata fixups (global string initializers, e.g. char *s = "hello"). */
    for (int i = 0; i < g_n_data_rodata_fixups; i++) {
        data_rodata_fixup_t *f = &g_data_rodata_fixups[i];
        uint64_t addr = rodata_vaddr + f->rodata_off;
        /* Write 8-byte little-endian address into g_data at data_off. */
        for (int j = 0; j < 8; j++) {
            g_data.data[f->data_off + j] = (uint8_t)(addr >> (8 * j));
        }
    }

    /* Patch data-symbol fixups (address-of-global initializers). */
    for (int i = 0; i < g_n_data_sym_fixups; i++) {
        data_sym_fixup_t *f = &g_data_sym_fixups[i];
        if (f->sym_idx < 0 || f->sym_idx >= g_n_globals) continue;
        uint64_t addr = g_globals[f->sym_idx].vaddr;
        for (int j = 0; j < 8; j++) {
            g_data.data[f->data_off + j] = (uint8_t)(addr >> (8 * j));
        }
    }

    /* Determine entry point. */
    if (g_entry == 0) {
        int idx = symtab_lookup_global(entry_sym ? entry_sym : "_start");
        if (idx >= 0 && g_globals[idx].kind == SYM_FUNCTION) {
            g_entry = g_globals[idx].vaddr;
        } else {
            cc_warn("entry symbol '%s' not found; entry=0", entry_sym);
        }
    }

    if (g_opts.verbose) {
        fprintf(stderr, "gen_finalize: text=%zu rodata=%zu data=%zu bss=%llu entry=0x%llx\n",
                g_text.size, g_rodata.size, g_data.size,
                (unsigned long long)g_bss_size, (unsigned long long)g_entry);
    }
}
