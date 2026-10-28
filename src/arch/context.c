/* Context layout matches libonyxc's RV64 LP64D jmp_buf:
 * SP, s0..s11, fs0..fs11, fcsr, continuation PC (27 eight-byte words).
 * Inline operations do not save caller-saved registers or signal masks. */
#include "core/compat.h"
#include "core/cc.h"
#include "arch/riscv64.h"
#include "arch/context.h"

static int saved_register(int index) {
    return index < 2 ? RV_S0 + index : RV_S2 + index - 2;
}

void rv_setjmp(int env_reg) {
    int base = RV_T6;
    rv_mv(base, env_reg);
    rv_sd(RV_SP, base, 0);
    for (int i = 0; i < 12; i++) {
        int reg = saved_register(i);
        rv_sd(reg, base, 8 + 8 * i);
        /* Integer and floating saved registers share ABI register numbers. */
        rv_fsd(reg, base, 104 + 8 * i);
    }
    /* csrrs t5,fcsr,zero: read without changing floating control/status. */
    cc_buf_push32(&g_text, (3u << 20) | (2u << 12) | (RV_T5 << 7) | 0x73u);
    rv_sd(RV_T5, base, 200);
    /* All four instructions before the continuation are fixed-width.
     * Resuming skips the initial zero result and reuses longjmp's A0. */
    rv_auipc(RV_T5, 0);
    rv_addi(RV_T5, RV_T5, 16);
    rv_sd(RV_T5, base, 208);
    rv_addi(RV_A0, RV_ZERO, 0);
    rv_mv(RV_T0, RV_A0);
}

void rv_longjmp(int env_reg, int value_reg) {
    /* Copy the environment before changing A0, using a scratch distinct
     * from value_reg even when callers supplied either scratch register. */
    int base = value_reg == RV_T6 ? RV_T4 : RV_T6;
    rv_mv(base, env_reg);
    rv_mv(RV_A0, value_reg);
    rv_seqz(RV_T0, RV_A0);
    rv_add(RV_A0, RV_A0, RV_T0); /* longjmp(env,0) returns one. */
    rv_ld(RV_T5, base, 200);
    /* csrrw zero,fcsr,t5: restore the floating environment. */
    cc_buf_push32(&g_text, (3u << 20) | (RV_T5 << 15) | (1u << 12) | 0x73u);
    rv_ld(RV_T5, base, 208);
    for (int i = 0; i < 12; i++) {
        int reg = saved_register(i);
        rv_ld(reg, base, 8 + 8 * i);
        rv_fld(reg, base, 104 + 8 * i);
    }
    rv_ld(RV_SP, base, 0);
    rv_jalr(RV_ZERO, RV_T5, 0);
}
