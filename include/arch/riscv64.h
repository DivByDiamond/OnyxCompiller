/*
 * riscv64.h — RISC-V (RV64IMAFD) instruction encoders.
 *
 * Subset sufficient for C/C++ codegen:
 *   - I-type: addi, slti, sltiu, xori, ori, andi, slli, srli, srai,
 *             lb, lbu, lh, lhu, lw, lwu, ld, jalr, flw, fld
 *   - R-type: add, sub, sll, slt, sltu, xor, srl, sra, or, and,
 *             mul, mulh, mulhu, div, divu, rem, remu,
 *             fadd/d, fsub/d, fmul/d, fdiv/d, fmv, fcvt, feq/flt/fle
 *   - S-type: sb, sh, sw, sd, fsw, fsd
 *   - B-type: beq, bne, blt, bge, bltu, bgeu
 *   - U-type: lui, auipc
 *   - J-type: jal
 *   - FENCE, ECALL, EBREAK, NOP
 *
 * Branches and jumps use labels; the codegen emits fixups that are
 * resolved at function end.
 */
#ifndef CC_RISCV64_H
#define CC_RISCV64_H

#include "core/cc.h"

/* Integer register names — ABI convention. */
#define RV_ZERO  0
#define RV_RA    1
#define RV_SP    2
#define RV_GP    3
#define RV_TP    4
#define RV_T0    5
#define RV_T1    6
#define RV_T2    7
#define RV_S0    8
#define RV_S1    9
#define RV_A0   10
#define RV_A1   11
#define RV_A2   12
#define RV_A3   13
#define RV_A4   14
#define RV_A5   15
#define RV_A6   16
#define RV_A7   17
#define RV_S2   18
#define RV_S3   19
#define RV_S4   20
#define RV_S5   21
#define RV_S6   22
#define RV_S7   23
#define RV_S8   24
#define RV_S9   25
#define RV_S10  26
#define RV_S11  27
#define RV_T3   28
#define RV_T4   29
#define RV_T5   30
#define RV_T6   31

#define RV_FP   RV_S0   /* frame pointer alias */

/* Float register names (separate register file, same numbers). */
#define RV_FT0   0
#define RV_FT1   1
#define RV_FT2   2
#define RV_FT3   3
#define RV_FT4   4
#define RV_FT5   5
#define RV_FT6   6
#define RV_FT7   7
#define RV_FS0   8
#define RV_FS1   9
#define RV_FA0  10
#define RV_FA1  11
#define RV_FA2  12
#define RV_FA3  13
#define RV_FA4  14
#define RV_FA5  15
#define RV_FA6  16
#define RV_FA7  17
#define RV_FS2  18
#define RV_FS3  19
#define RV_FS4  20
#define RV_FS5  21
#define RV_FS6  22
#define RV_FS7  23
#define RV_FS8  24
#define RV_FS9  25
#define RV_FS10 26
#define RV_FS11 27
#define RV_FT8  28
#define RV_FT9  29
#define RV_FT10 30
#define RV_FT11 31

/* Fixup records for forward branches/jumps. */
typedef enum {
    FX_BRANCH,   /* 13-bit immediate, B-type */
    FX_JUMP,     /* 21-bit immediate, J-type */
} fixup_kind_t;

typedef struct fixup {
    fixup_kind_t kind;
    uint32_t patch_off;   /* offset in g_text where the instruction is */
    uint32_t target_off;  /* offset of the label (target) */
    struct fixup *next;
} fixup_t;

/* ---- Integer instructions -------------------------------------------- */
void rv_nop(void);
void rv_addi(int rd, int rs1, int imm);
void rv_addi_imm(int rd, int rs1, int64_t imm);
void rv_add(int rd, int rs1, int rs2);
void rv_sub(int rd, int rs1, int rs2);
void rv_mul(int rd, int rs1, int rs2);
void rv_div(int rd, int rs1, int rs2);
void rv_divu(int rd, int rs1, int rs2);
void rv_rem(int rd, int rs1, int rs2);
void rv_remu(int rd, int rs1, int rs2);
void rv_and(int rd, int rs1, int rs2);
void rv_or(int rd, int rs1, int rs2);
void rv_xor(int rd, int rs1, int rs2);
void rv_sll(int rd, int rs1, int rs2);
void rv_srl(int rd, int rs1, int rs2);
void rv_sra(int rd, int rs1, int rs2);
void rv_slt(int rd, int rs1, int rs2);
void rv_sltu(int rd, int rs1, int rs2);
void rv_slti(int rd, int rs1, int imm);
void rv_sltiu(int rd, int rs1, int imm);
void rv_slli(int rd, int rs1, int shamt);
void rv_srli(int rd, int rs1, int shamt);
void rv_srai(int rd, int rs1, int shamt);

void rv_lb(int rd, int rs1, int imm);
void rv_lbu(int rd, int rs1, int imm);
void rv_lh(int rd, int rs1, int imm);
void rv_lhu(int rd, int rs1, int imm);
void rv_lw(int rd, int rs1, int imm);
void rv_lwu(int rd, int rs1, int imm);
void rv_ld(int rd, int rs1, int imm);
void rv_sb(int rs2, int rs1, int imm);
void rv_sh(int rs2, int rs1, int imm);
void rv_sw(int rs2, int rs1, int imm);
void rv_sd(int rs2, int rs1, int imm);

void rv_lui(int rd, int imm20);
void rv_auipc(int rd, int imm20);
void rv_jal(int rd, int32_t off21);
void rv_jalr(int rd, int rs1, int imm);

void rv_beq(int rs1, int rs2, int32_t off13);
void rv_bne(int rs1, int rs2, int32_t off13);
void rv_blt(int rs1, int rs2, int32_t off13);
void rv_bge(int rs1, int rs2, int32_t off13);
void rv_bltu(int rs1, int rs2, int32_t off13);
void rv_bgeu(int rs1, int rs2, int32_t off13);

void rv_ecall(void);
void rv_ebreak(void);
void rv_fence(void);
void rv_ret(void);
void rv_mv(int rd, int rs);
void rv_neg(int rd, int rs);
void rv_not(int rd, int rs);
void rv_seqz(int rd, int rs);
void rv_snez(int rd, int rs);

/* Branch/jump to a label not yet known. */
void rv_branch(int rs1, int rs2, int op_token, uint32_t *label_out);
void rv_jal_label(int rd, uint32_t *label_out);
void rv_label_set(uint32_t *label);

/* ---- F/D extension (float / double) ---------------------------------- */

/* Load/store. */
void rv_flw(int rd, int rs1, int imm);
void rv_fld(int rd, int rs1, int imm);
void rv_fsw(int rs2, int rs1, int imm);
void rv_fsd(int rs2, int rs1, int imm);

/* Arithmetic — single-precision. */
void rv_fadd_s(int rd, int rs1, int rs2);
void rv_fsub_s(int rd, int rs1, int rs2);
void rv_fmul_s(int rd, int rs1, int rs2);
void rv_fdiv_s(int rd, int rs1, int rs2);

/* Arithmetic — double-precision. */
void rv_fadd_d(int rd, int rs1, int rs2);
void rv_fsub_d(int rd, int rs1, int rs2);
void rv_fmul_d(int rd, int rs1, int rs2);
void rv_fdiv_d(int rd, int rs1, int rs2);

/* Negate. */
void rv_fneg_s(int rd, int rs1);
void rv_fneg_d(int rd, int rs1);

/* Move between int and float register files. */
void rv_fmv_w_x(int rd, int rs1);
void rv_fmv_x_w(int rd, int rs1);
void rv_fmv_d_x(int rd, int rs1);
void rv_fmv_x_d(int rd, int rs1);

/* Conversions. */
void rv_fcvt_s_l(int rd, int rs1);
void rv_fcvt_l_s(int rd, int rs1);
void rv_fcvt_d_l(int rd, int rs1);
void rv_fcvt_l_d(int rd, int rs1);
void rv_fcvt_s_d(int rd, int rs1);
void rv_fcvt_d_s(int rd, int rs1);
void rv_fcvt_s_w(int rd, int rs1);
void rv_fcvt_w_s(int rd, int rs1);

/* Comparisons — single-precision (result in integer rd). */
void rv_feq_s(int rd, int rs1, int rs2);
void rv_flt_s(int rd, int rs1, int rs2);
void rv_fle_s(int rd, int rs1, int rs2);

/* Comparisons — double-precision (result in integer rd). */
void rv_feq_d(int rd, int rs1, int rs2);
void rv_flt_d(int rd, int rs1, int rs2);
void rv_fle_d(int rd, int rs1, int rs2);

/* General FPU R-type with raw funct7 (extensibility). */
void rv_emit_fpu_r(uint32_t funct7, int funct3, int rd, int rs1, int rs2);

/* Resolve all pending fixups in this function (called at function end). */
void rv_resolve_fixups(void);

#endif /* CC_RISCV64_H */
