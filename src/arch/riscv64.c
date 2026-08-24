/*
 * riscv64.c — RISC-V (RV64IMAFD) instruction encoders.
 *
 * Instructions are emitted little-endian into g_text. Branches and
 * jumps use label-based fixups resolved at function end.
 */
#include "core/compat.h"

#include "core/cc.h"
#include "front/lexer.h"
#include "arch/riscv64.h"

/* ---- Fixup list ------------------------------------------------------ */
static fixup_t *g_fixups_head = NULL;
static fixup_t *g_fixups_tail = NULL;

static fixup_t *new_fixup(fixup_kind_t k, uint32_t patch_off, uint32_t target_off) {
    fixup_t *f = (fixup_t *)malloc(sizeof(fixup_t));
    if (!f) cc_fatal("oom fixup");
    f->kind = k;
    f->patch_off = patch_off;
    f->target_off = target_off;
    f->next = NULL;
    if (g_fixups_tail) g_fixups_tail->next = f;
    else g_fixups_head = f;
    g_fixups_tail = f;
    return f;
}

void rv_resolve_fixups(void) {
    fixup_t *f = g_fixups_head;
    while (f) {
        int32_t delta = (int32_t)f->target_off - (int32_t)f->patch_off;
        uint8_t *p = g_text.data + f->patch_off;
        if (f->kind == FX_BRANCH) {
            /* Patch B-type immediate. */
            uint32_t insn = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                            ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
            /* Clear existing imm bits. */
            insn &= ~((uint32_t)0xFE000F80);
            int32_t imm = delta;
            uint32_t b = ((uint32_t)(imm & 0x1000) << 19) |
                         ((uint32_t)(imm & 0x7E0) << 20) |
                         ((uint32_t)(imm & 0x1E) << 7)  |
                         ((uint32_t)(imm & 0x800) >> 4);
            insn |= b;
            p[0] = insn & 0xff; p[1] = (insn >> 8) & 0xff;
            p[2] = (insn >> 16) & 0xff; p[3] = (insn >> 24) & 0xff;
        } else { /* FX_JUMP */
            uint32_t insn = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                            ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
            insn &= ~0xFFFFF000;
            int32_t imm = delta;
            uint32_t b = ((uint32_t)(imm & 0x100000) << 11) |
                         ((uint32_t)(imm & 0x7FE) << 20) |
                         ((uint32_t)(imm & 0x800) << 9)  |
                         ((uint32_t)(imm & 0xFF000));
            insn |= b;
            p[0] = insn & 0xff; p[1] = (insn >> 8) & 0xff;
            p[2] = (insn >> 16) & 0xff; p[3] = (insn >> 24) & 0xff;
        }
        fixup_t *next = f->next;
        free(f);
        f = next;
    }
    g_fixups_head = g_fixups_tail = NULL;
}

void rv_label_set(uint32_t *label) {
    *label = (uint32_t)g_text.size;
}

/* ---- Emit raw instruction ------------------------------------------- */
static void emit32(uint32_t insn) {
    cc_buf_push32(&g_text, insn);
}

/* ---- Encoders -------------------------------------------------------- */
void rv_nop(void) { emit32(0x00000013); }

void rv_addi(int rd, int rs1, int imm) {
    uint32_t insn = (uint32_t)(imm & 0xFFF) << 20
                  | (uint32_t)(rs1 & 0x1F) << 15
                  | 0x0 << 12
                  | (uint32_t)(rd & 0x1F) << 7
                  | 0x13;
    emit32(insn);
}

/* addi-imm: emit lui+addi if needed for constants outside [-2048, 2047]. */
void rv_addi_imm(int rd, int rs1, int64_t imm) {
    if (imm >= -2048 && imm <= 2047) {
        rv_addi(rd, rs1, (int)imm);
        return;
    }
    /* Materialize imm into t6, then add. */
    int tmp = RV_T6;
    int64_t v = imm;
    int64_t lo = (int64_t)(int32_t)(v & 0xFFF);
    int64_t hi = (v - lo) & 0xFFFFFFFFFFFFF000LL;
    /* lui tmp, hi[31:12] */
    rv_lui(tmp, (uint32_t)(hi >> 12) & 0xFFFFF);
    rv_addi(tmp, tmp, (int)lo);
    rv_add(rd, rs1, tmp);
}

void rv_lui(int rd, int imm20) {
    uint32_t insn = (uint32_t)(imm20 & 0xFFFFF) << 12
                  | (uint32_t)(rd & 0x1F) << 7
                  | 0x37;
    emit32(insn);
}

void rv_auipc(int rd, int imm20) {
    uint32_t insn = (uint32_t)(imm20 & 0xFFFFF) << 12
                  | (uint32_t)(rd & 0x1F) << 7
                  | 0x17;
    emit32(insn);
}

static void emit_r_type(int funct3, int funct7, int rd, int rs1, int rs2, int opcode) {
    uint32_t insn = (uint32_t)(funct7 & 0x7F) << 25
                  | (uint32_t)(rs2 & 0x1F) << 20
                  | (uint32_t)(rs1 & 0x1F) << 15
                  | (uint32_t)(funct3 & 0x7) << 12
                  | (uint32_t)(rd & 0x1F) << 7
                  | (uint32_t)(opcode & 0x7F);
    emit32(insn);
}

void rv_add(int rd, int rs1, int rs2)  { emit_r_type(0x0, 0x00, rd, rs1, rs2, 0x33); }
void rv_sub(int rd, int rs1, int rs2)  { emit_r_type(0x0, 0x20, rd, rs1, rs2, 0x33); }
void rv_sll(int rd, int rs1, int rs2)  { emit_r_type(0x1, 0x00, rd, rs1, rs2, 0x33); }
void rv_slt(int rd, int rs1, int rs2)  { emit_r_type(0x2, 0x00, rd, rs1, rs2, 0x33); }
void rv_sltu(int rd, int rs1, int rs2) { emit_r_type(0x3, 0x00, rd, rs1, rs2, 0x33); }
void rv_xor(int rd, int rs1, int rs2)  { emit_r_type(0x4, 0x00, rd, rs1, rs2, 0x33); }
void rv_srl(int rd, int rs1, int rs2)  { emit_r_type(0x5, 0x00, rd, rs1, rs2, 0x33); }
void rv_sra(int rd, int rs1, int rs2)  { emit_r_type(0x5, 0x20, rd, rs1, rs2, 0x33); }
void rv_or(int rd, int rs1, int rs2)   { emit_r_type(0x6, 0x00, rd, rs1, rs2, 0x33); }
void rv_and(int rd, int rs1, int rs2)  { emit_r_type(0x7, 0x00, rd, rs1, rs2, 0x33); }

/* M extension */
void rv_mul(int rd, int rs1, int rs2)   { emit_r_type(0x0, 0x01, rd, rs1, rs2, 0x33); }
void rv_mulh(int rd, int rs1, int rs2)  { emit_r_type(0x1, 0x01, rd, rs1, rs2, 0x33); }
void rv_mulhsu(int rd, int rs1, int rs2){ emit_r_type(0x2, 0x01, rd, rs1, rs2, 0x33); }
void rv_mulhu(int rd, int rs1, int rs2) { emit_r_type(0x3, 0x01, rd, rs1, rs2, 0x33); }
void rv_div(int rd, int rs1, int rs2)   { emit_r_type(0x4, 0x01, rd, rs1, rs2, 0x33); }
void rv_divu(int rd, int rs1, int rs2)  { emit_r_type(0x5, 0x01, rd, rs1, rs2, 0x33); }
void rv_rem(int rd, int rs1, int rs2)   { emit_r_type(0x6, 0x01, rd, rs1, rs2, 0x33); }
void rv_remu(int rd, int rs1, int rs2)  { emit_r_type(0x7, 0x01, rd, rs1, rs2, 0x33); }

void rv_slti(int rd, int rs1, int imm) {
    uint32_t insn = (uint32_t)(imm & 0xFFF) << 20
                  | (uint32_t)(rs1 & 0x1F) << 15
                  | 0x2 << 12
                  | (uint32_t)(rd & 0x1F) << 7
                  | 0x13;
    emit32(insn);
}
void rv_sltiu(int rd, int rs1, int imm) {
    uint32_t insn = (uint32_t)(imm & 0xFFF) << 20
                  | (uint32_t)(rs1 & 0x1F) << 15
                  | 0x3 << 12
                  | (uint32_t)(rd & 0x1F) << 7
                  | 0x13;
    emit32(insn);
}

/* Shift-immediate: I-type, funct3 distinguishes slli/srli/srai.
 * slli: funct3=0x1, imm[11:5]=0x00
 * srli: funct3=0x5, imm[11:5]=0x00
 * srai: funct3=0x5, imm[11:5]=0x20
 * For RV64, shamt is 6 bits (imm[5:0]), imm[11:6] carries the funct7 bits.
 */
void rv_slli(int rd, int rs1, int shamt) {
    uint32_t insn = ((uint32_t)(shamt & 0x3F) << 20)
                  | (uint32_t)(rs1 & 0x1F) << 15
                  | 0x1 << 12
                  | (uint32_t)(rd & 0x1F) << 7
                  | 0x13;
    emit32(insn);
}
void rv_srli(int rd, int rs1, int shamt) {
    uint32_t insn = ((uint32_t)(shamt & 0x3F) << 20)
                  | (uint32_t)(rs1 & 0x1F) << 15
                  | 0x5 << 12
                  | (uint32_t)(rd & 0x1F) << 7
                  | 0x13;
    emit32(insn);
}
void rv_srai(int rd, int rs1, int shamt) {
    uint32_t insn = (0x20 << 25)
                  | ((uint32_t)(shamt & 0x3F) << 20)
                  | (uint32_t)(rs1 & 0x1F) << 15
                  | 0x5 << 12
                  | (uint32_t)(rd & 0x1F) << 7
                  | 0x13;
    emit32(insn);
}

/* Loads: opcode 0x03 */
static void emit_i_load(int funct3, int rd, int rs1, int imm) {
    uint32_t insn = (uint32_t)(imm & 0xFFF) << 20
                  | (uint32_t)(rs1 & 0x1F) << 15
                  | (uint32_t)(funct3 & 0x7) << 12
                  | (uint32_t)(rd & 0x1F) << 7
                  | 0x03;
    emit32(insn);
}
void rv_lb(int rd, int rs1, int imm)  { emit_i_load(0x0, rd, rs1, imm); }
void rv_lh(int rd, int rs1, int imm)  { emit_i_load(0x1, rd, rs1, imm); }
void rv_lw(int rd, int rs1, int imm)  { emit_i_load(0x2, rd, rs1, imm); }
void rv_ld(int rd, int rs1, int imm)  { emit_i_load(0x3, rd, rs1, imm); }
void rv_lbu(int rd, int rs1, int imm) { emit_i_load(0x4, rd, rs1, imm); }
void rv_lhu(int rd, int rs1, int imm) { emit_i_load(0x5, rd, rs1, imm); }
void rv_lwu(int rd, int rs1, int imm) { emit_i_load(0x6, rd, rs1, imm); }

/* Stores: opcode 0x23 */
static void emit_s_store(int funct3, int rs2, int rs1, int imm) {
    uint32_t imm_lo = (uint32_t)(imm & 0x1F);
    uint32_t imm_hi = (uint32_t)((imm >> 5) & 0x7F);
    uint32_t insn = imm_hi << 25
                  | (uint32_t)(rs2 & 0x1F) << 20
                  | (uint32_t)(rs1 & 0x1F) << 15
                  | (uint32_t)(funct3 & 0x7) << 12
                  | imm_lo << 7
                  | 0x23;
    emit32(insn);
}
void rv_sb(int rs2, int rs1, int imm) { emit_s_store(0x0, rs2, rs1, imm); }
void rv_sh(int rs2, int rs1, int imm) { emit_s_store(0x1, rs2, rs1, imm); }
void rv_sw(int rs2, int rs1, int imm) { emit_s_store(0x2, rs2, rs1, imm); }
void rv_sd(int rs2, int rs1, int imm) { emit_s_store(0x3, rs2, rs1, imm); }

/* Branches: opcode 0x63 */
static void emit_b(int funct3, int rs1, int rs2, int32_t off13) {
    uint32_t imm = (uint32_t)off13;
    uint32_t b12 = (imm & 0x1000) >> 12;
    uint32_t b11 = (imm & 0x800) >> 11;
    uint32_t b10_5 = (imm & 0x7E0) >> 5;
    uint32_t b4_1 = (imm & 0x1E) >> 1;
    uint32_t insn = b12 << 31
                  | b10_5 << 25
                  | (uint32_t)(rs2 & 0x1F) << 20
                  | (uint32_t)(rs1 & 0x1F) << 15
                  | (uint32_t)(funct3 & 0x7) << 12
                  | b4_1 << 8
                  | b11 << 7
                  | 0x63;
    emit32(insn);
}
void rv_beq(int rs1, int rs2, int32_t off13)  { emit_b(0x0, rs1, rs2, off13); }
void rv_bne(int rs1, int rs2, int32_t off13)  { emit_b(0x1, rs1, rs2, off13); }
void rv_blt(int rs1, int rs2, int32_t off13)  { emit_b(0x4, rs1, rs2, off13); }
void rv_bge(int rs1, int rs2, int32_t off13)  { emit_b(0x5, rs1, rs2, off13); }
void rv_bltu(int rs1, int rs2, int32_t off13) { emit_b(0x6, rs1, rs2, off13); }
void rv_bgeu(int rs1, int rs2, int32_t off13) { emit_b(0x7, rs1, rs2, off13); }

void rv_jal(int rd, int32_t off21) {
    uint32_t imm = (uint32_t)off21;
    uint32_t b20 = (imm & 0x100000) >> 20;
    uint32_t b10_1 = (imm & 0x7FE) >> 1;
    uint32_t b11 = (imm & 0x800) >> 11;
    uint32_t b19_12 = (imm & 0xFF000) >> 12;
    uint32_t insn = b20 << 31
                  | b10_1 << 21
                  | b11 << 20
                  | b19_12 << 12
                  | (uint32_t)(rd & 0x1F) << 7
                  | 0x6F;
    emit32(insn);
}

void rv_jalr(int rd, int rs1, int imm) {
    uint32_t insn = (uint32_t)(imm & 0xFFF) << 20
                  | (uint32_t)(rs1 & 0x1F) << 15
                  | 0x0 << 12
                  | (uint32_t)(rd & 0x1F) << 7
                  | 0x67;
    emit32(insn);
}

void rv_ecall(void)  { emit32(0x00000073); }
void rv_ebreak(void) { emit32(0x00100073); }
void rv_fence(void)  { emit32(0x0FF0000F); }
void rv_ret(void)    { rv_jalr(RV_ZERO, RV_RA, 0); }
void rv_mv(int rd, int rs)    { rv_addi(rd, rs, 0); }
void rv_neg(int rd, int rs)   { rv_sub(rd, RV_ZERO, rs); }
void rv_not(int rd, int rs)   { rv_addi(rd, rs, -1); }
void rv_seqz(int rd, int rs)  { rv_sltiu(rd, rs, 1); }
void rv_snez(int rd, int rs)  { rv_sltu(rd, RV_ZERO, rs); }

/* ---- F/D extension (float / double) --------------------------------- */

/* General float R-type emitter (opcode = OP-FP = 0x53).
 * funct7 and rs2 together specify the FPU operation; funct3 is the
 * rounding mode (0=RNE, 1=RTZ, …). */
void rv_emit_fpu_r(uint32_t funct7, int funct3, int rd, int rs1, int rs2) {
    uint32_t insn = (funct7 & 0x7F) << 25
                  | (uint32_t)(rs2 & 0x1F) << 20
                  | (uint32_t)(rs1 & 0x1F) << 15
                  | (uint32_t)(funct3 & 0x7) << 12
                  | (uint32_t)(rd & 0x1F) << 7
                  | 0x53;
    emit32(insn);
}

/* Load/store float/double. */
void rv_flw(int rd, int rs1, int imm) {
    uint32_t insn = (uint32_t)(imm & 0xFFF) << 20
                  | (uint32_t)(rs1 & 0x1F) << 15
                  | 0x2 << 12
                  | (uint32_t)(rd & 0x1F) << 7
                  | 0x07;
    emit32(insn);
}

void rv_fld(int rd, int rs1, int imm) {
    uint32_t insn = (uint32_t)(imm & 0xFFF) << 20
                  | (uint32_t)(rs1 & 0x1F) << 15
                  | 0x3 << 12
                  | (uint32_t)(rd & 0x1F) << 7
                  | 0x07;
    emit32(insn);
}

void rv_fsw(int rs2, int rs1, int imm) {
    uint32_t imm_lo = (uint32_t)(imm & 0x1F);
    uint32_t imm_hi = (uint32_t)((imm >> 5) & 0x7F);
    uint32_t insn = imm_hi << 25
                  | (uint32_t)(rs2 & 0x1F) << 20
                  | (uint32_t)(rs1 & 0x1F) << 15
                  | 0x2 << 12
                  | imm_lo << 7
                  | 0x27;
    emit32(insn);
}

void rv_fsd(int rs2, int rs1, int imm) {
    uint32_t imm_lo = (uint32_t)(imm & 0x1F);
    uint32_t imm_hi = (uint32_t)((imm >> 5) & 0x7F);
    uint32_t insn = imm_hi << 25
                  | (uint32_t)(rs2 & 0x1F) << 20
                  | (uint32_t)(rs1 & 0x1F) << 15
                  | 0x3 << 12
                  | imm_lo << 7
                  | 0x27;
    emit32(insn);
}

/* Arithmetic — single-precision. funct3=0 (RNE). */
void rv_fadd_s(int rd, int rs1, int rs2) { rv_emit_fpu_r(0x00, 0x0, rd, rs1, rs2); }
void rv_fsub_s(int rd, int rs1, int rs2) { rv_emit_fpu_r(0x08, 0x0, rd, rs1, rs2); }
void rv_fmul_s(int rd, int rs1, int rs2) { rv_emit_fpu_r(0x10, 0x0, rd, rs1, rs2); }
void rv_fdiv_s(int rd, int rs1, int rs2) { rv_emit_fpu_r(0x18, 0x0, rd, rs1, rs2); }

/* Arithmetic — double-precision. funct3=1 (RNE for double). */
void rv_fadd_d(int rd, int rs1, int rs2) { rv_emit_fpu_r(0x00, 0x1, rd, rs1, rs2); }
void rv_fsub_d(int rd, int rs1, int rs2) { rv_emit_fpu_r(0x08, 0x1, rd, rs1, rs2); }
void rv_fmul_d(int rd, int rs1, int rs2) { rv_emit_fpu_r(0x10, 0x1, rd, rs1, rs2); }
void rv_fdiv_d(int rd, int rs1, int rs2) { rv_emit_fpu_r(0x18, 0x1, rd, rs1, rs2); }

/* Move between int and float register files. */
void rv_fmv_w_x(int rd, int rs1) { rv_emit_fpu_r(0xF8, 0x0, rd, rs1, 0); }
void rv_fmv_x_w(int rd, int rs1) { rv_emit_fpu_r(0xE0, 0x0, rd, rs1, 0); }
void rv_fmv_d_x(int rd, int rs1) { rv_emit_fpu_r(0xF2, 0x0, rd, rs1, 0); }
void rv_fmv_x_d(int rd, int rs1) { rv_emit_fpu_r(0xE2, 0x1, rd, rs1, 0); }

/* Conversions — verified against Spike riscv/encoding.h MATCH_* constants.
 *   fcvt.s.l: funct7=0x68, rs2=0x2, funct3=0 (RNE)
 *   fcvt.l.s: funct7=0x60, rs2=0x2, funct3=1 (RTZ)
 *   fcvt.d.l: funct7=0x69, rs2=0x2, funct3=0 (RNE)
 *   fcvt.l.d: funct7=0x61, rs2=0x2, funct3=1 (RTZ)
 *   fcvt.s.d: funct7=0x20, rs2=0x1, funct3=0 (RNE)
 *   fcvt.d.s: funct7=0x21, rs2=0x0, funct3=0 (RNE)
 *   fcvt.s.w: funct7=0x68, rs2=0x0, funct3=0 (RNE)
 *   fcvt.w.s: funct7=0x60, rs2=0x0, funct3=1 (RTZ)
 */
void rv_fcvt_s_l(int rd, int rs1) { rv_emit_fpu_r(0x68, 0x0, rd, rs1, 0x2); }
void rv_fcvt_l_s(int rd, int rs1) { rv_emit_fpu_r(0x60, 0x1, rd, rs1, 0x2); }
void rv_fcvt_d_l(int rd, int rs1) { rv_emit_fpu_r(0x69, 0x0, rd, rs1, 0x2); }
void rv_fcvt_l_d(int rd, int rs1) { rv_emit_fpu_r(0x61, 0x1, rd, rs1, 0x2); }
void rv_fcvt_s_d(int rd, int rs1) { rv_emit_fpu_r(0x20, 0x0, rd, rs1, 0x1); }
void rv_fcvt_d_s(int rd, int rs1) { rv_emit_fpu_r(0x21, 0x0, rd, rs1, 0x0); }
void rv_fcvt_s_w(int rd, int rs1) { rv_emit_fpu_r(0x68, 0x0, rd, rs1, 0x0); }
void rv_fcvt_w_s(int rd, int rs1) { rv_emit_fpu_r(0x60, 0x1, rd, rs1, 0x0); }

/* Comparisons — single-precision. rd is an int register.
 *   feq.s: funct7=0x50, funct3=0x2
 *   flt.s: funct7=0x50, funct3=0x1
 *   fle.s: funct7=0x50, funct3=0x0
 */
void rv_feq_s(int rd, int rs1, int rs2) { rv_emit_fpu_r(0x50, 0x2, rd, rs1, rs2); }
void rv_flt_s(int rd, int rs1, int rs2) { rv_emit_fpu_r(0x50, 0x1, rd, rs1, rs2); }
void rv_fle_s(int rd, int rs1, int rs2) { rv_emit_fpu_r(0x50, 0x0, rd, rs1, rs2); }

/* Comparisons — double-precision.
 *   feq.d: funct7=0x51, funct3=0x2
 *   flt.d: funct7=0x51, funct3=0x1
 *   fle.d: funct7=0x51, funct3=0x0
 */
void rv_feq_d(int rd, int rs1, int rs2) { rv_emit_fpu_r(0x51, 0x2, rd, rs1, rs2); }
void rv_flt_d(int rd, int rs1, int rs2) { rv_emit_fpu_r(0x51, 0x1, rd, rs1, rs2); }
void rv_fle_d(int rd, int rs1, int rs2) { rv_emit_fpu_r(0x51, 0x0, rd, rs1, rs2); }

/* Float neg — flip sign bit via fsgnjn.
 *   fneg.s rd, rs = fsgnjn.s rd, rs, rs (funct7=0x10, funct3=0x1)
 *   fneg.d rd, rs = fsgnjn.d rd, rs, rs (funct7=0x11, funct3=0x1)
 */
void rv_fneg_s(int rd, int rs1) { rv_emit_fpu_r(0x10, 0x1, rd, rs1, rs1); }
void rv_fneg_d(int rd, int rs1) { rv_emit_fpu_r(0x11, 0x1, rd, rs1, rs1); }

/* ---- Branch / label helpers (unchanged from IMA) --------------------- */

/* Token-based branch (used by codegen for if/while conditions). */
void rv_branch(int rs1, int rs2, int op_token, uint32_t *label_out) {
    uint32_t patch = (uint32_t)g_text.size;
    int32_t target = (int32_t)*label_out;
    int32_t delta = target - (int32_t)patch;
    switch (op_token) {
        case T_EQ: rv_beq(rs1, rs2, delta); break;
        case T_NE: rv_bne(rs1, rs2, delta); break;
        case T_LT: rv_blt(rs1, rs2, delta); break;
        case T_GT: rv_blt(rs2, rs1, delta); break;
        case T_LE: rv_bge(rs2, rs1, delta); break;
        case T_GE: rv_bge(rs1, rs2, delta); break;
        default:
            rv_bne(rs1, rs2, delta);
            break;
    }
    if (target == 0) {
        new_fixup(FX_BRANCH, patch, 0);
        *label_out = patch;
    }
}

void rv_jal_label(int rd, uint32_t *label_out) {
    uint32_t patch = (uint32_t)g_text.size;
    int32_t target = (int32_t)*label_out;
    int32_t delta = target - (int32_t)patch;
    rv_jal(rd, delta);
    if (target == 0) {
        new_fixup(FX_JUMP, patch, 0);
        *label_out = patch;
    }
}
