/*
 * obj_reloc.c — ONYO relocation emission for `onyxcc -c`.
 *
 * One deferred compiler fixup becomes one or two ONYO relocations; the
 * target is the object symbol index computed by obj.c (section symbols
 * for .rodata references, real symbols otherwise). Mapping table:
 * docs/plan-separate-compilation.md.
 */
#include "core/compat.h"

#include "core/cc.h"
#include "front/ast.h"
#include "back/gen.h"
#include "back/obj.h"
#include "sys/onyxo.h"

/* The ONYO reloc addend field is a signed 32-bit integer. */
#define OBJ_ADDEND_MIN (-2147483647LL - 1)
#define OBJ_ADDEND_MAX 2147483647LL

static void push_reloc(cc_buf_t *b, uint32_t section_idx, uint32_t type,
                       uint64_t offset, uint32_t sym_idx, int32_t addend) {
    cc_buf_push32(b, section_idx);
    cc_buf_push32(b, type);
    cc_buf_push64(b, offset);
    cc_buf_push32(b, sym_idx);
    cc_buf_push32(b, (uint32_t)addend);
}

/* Object symbol index for a g_globals index; -1 with a diagnostic when the
 * target cannot be represented (a compiler bug, not user input). */
static int32_t sym_target(const int32_t *sym_map, int n_globals, int sym_idx) {
    if (sym_idx >= 0 && sym_idx < n_globals && sym_map[sym_idx] >= 0)
        return sym_map[sym_idx];
    cc_error("obj: fixup target #%d has no object symbol", sym_idx);
    return -1;
}

int obj_write_relocs(cc_buf_t *relocs, const int32_t *sym_map, int n_globals) {
    /* Text -> symbol: the lui/addi pair of emit_load_global_addr(). */
    for (int i = 0; i < gen_addr_fixup_count(); i++) {
        int sym_idx;
        uint32_t patch_lui, patch_addi;
        int64_t extra_off;
        gen_addr_fixup_get(i, &sym_idx, &patch_lui, &patch_addi, &extra_off);
        if (sym_idx < 0) continue;   /* no target recorded; gen_finalize skips these too */
        int32_t target = sym_target(sym_map, n_globals, sym_idx);
        if (target < 0) return -1;
        if (extra_off < OBJ_ADDEND_MIN || extra_off > OBJ_ADDEND_MAX) {
            cc_error("obj: addend %lld for '%s' does not fit int32",
                     (long long)extra_off, g_globals[sym_idx].name);
            return -1;
        }
        push_reloc(relocs, ONYO_SEC_TEXT, R_ONYO_HI20, patch_lui,
                   (uint32_t)target, (int32_t)extra_off);
        push_reloc(relocs, ONYO_SEC_TEXT, R_ONYO_LO12_I, patch_addi,
                   (uint32_t)target, (int32_t)extra_off);
    }

    /* Text -> rodata (string literals), through the `.rodata` section
     * symbol; index of that symbol equals its section index. */
    for (int i = 0; i < gen_rodata_fixup_count(); i++) {
        uint32_t rodata_off, patch_lui, patch_addi;
        gen_rodata_fixup_get(i, &rodata_off, &patch_lui, &patch_addi);
        push_reloc(relocs, ONYO_SEC_TEXT, R_ONYO_HI20, patch_lui,
                   ONYO_SEC_RODATA, (int32_t)rodata_off);
        push_reloc(relocs, ONYO_SEC_TEXT, R_ONYO_LO12_I, patch_addi,
                   ONYO_SEC_RODATA, (int32_t)rodata_off);
    }

    /* Data -> rodata: `char *s = "..."` initializer slot. */
    for (int i = 0; i < gen_data_rodata_fixup_count(); i++) {
        uint32_t rodata_off, data_off;
        gen_data_rodata_fixup_get(i, &rodata_off, &data_off);
        push_reloc(relocs, ONYO_SEC_DATA, R_ONYO_64, data_off,
                   ONYO_SEC_RODATA, (int32_t)rodata_off);
    }

    /* Data -> symbol: `&global`, array/function decay initializers. */
    for (int i = 0; i < gen_data_sym_fixup_count(); i++) {
        int sym_idx;
        uint32_t data_off;
        gen_data_sym_fixup_get(i, &sym_idx, &data_off);
        if (sym_idx < 0) continue;
        int32_t target = sym_target(sym_map, n_globals, sym_idx);
        if (target < 0) return -1;
        push_reloc(relocs, ONYO_SEC_DATA, R_ONYO_64, data_off,
                   (uint32_t)target, 0);
    }
    return 0;
}
