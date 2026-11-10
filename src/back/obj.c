/*
 * obj.c — ONYO relocatable object writer (`onyxcc -c`).
 *
 * Turns the segments and deferred address patches accumulated by gen.c into
 * one ONYO object (contract: docs/plan-separate-compilation.md, layout:
 * include/sys/onyxo.h). Sections are always .text/.rodata/.data/.bss, in
 * that order; every unresolved reference becomes a relocation for onyx-ld,
 * so this file never calls gen_finalize() and never mutates gen state.
 * Section symbols, file-scope `static` objects and `__sl_*` function-local
 * statics are marked LOCAL so the linker keeps them out of the global space.
 */
#include "core/compat.h"

#include "core/cc.h"
#include "back/gen.h"
#include "back/obj.h"
#include "sys/onyxo.h"

/* Fixed section table: index == ONYO_SEC_* type; name, R/W/X flags, align.
 * Section symbols are emitted in the same order, so a section's symbol
 * index equals its type (onyx-ld needs `.rodata` as a reloc target). */
#define OBJ_N_SECS 4
static const struct {
    const char *name;
    uint32_t flags;
    uint32_t align;
} k_secs[OBJ_N_SECS] = {
    { ".text",   ONYO_SEC_R | ONYO_SEC_X,  4  },
    { ".rodata", ONYO_SEC_R,               16 },
    { ".data",   ONYO_SEC_R | ONYO_SEC_W,  16 },
    { ".bss",    ONYO_SEC_R | ONYO_SEC_W,  16 },
};

static uint32_t strtab_add(cc_buf_t *tab, const char *name) {
    uint32_t off = (uint32_t)tab->size;
    cc_buf_push(tab, name, strlen(name) + 1);
    return off;
}

static void push_symbol(cc_buf_t *b, uint32_t name_off, uint32_t flags,
                        int32_t section_idx, uint64_t value) {
    cc_buf_push32(b, name_off);
    cc_buf_push32(b, flags);
    cc_buf_push32(b, (uint32_t)section_idx);
    cc_buf_push32(b, 0);       /* reserved */
    cc_buf_push64(b, value);   /* offset within section_idx */
    cc_buf_push64(b, 0);       /* size: not needed by onyx-ld */
}

/* Undefined symbols are exported only when a relocation references them.
 * Headers declare many prototypes that a TU never calls (e.g. execvpe);
 * exporting those as UNDEF makes the linker reject perfectly good objects. */
static bool g_obj_referenced[CC_MAX_GLOBALS];

/* Emit one g_globals entry; false for kinds that never reach an object. */
static bool push_global_symbol(cc_buf_t *syms, cc_buf_t *strtab, int gidx) {
    sym_t *s = &g_globals[gidx];
    uint32_t flags;
    int32_t sec;
    uint64_t value = 0;
    if (s->kind == SYM_FUNCTION) {
        if (s->is_defined) {
            flags = ONYO_SYM_DEFINED | ONYO_SYM_FUNC |
                    (s->is_static ? ONYO_SYM_LOCAL : ONYO_SYM_GLOBAL);
            sec = ONYO_SEC_TEXT;
            value = s->text_off;
        } else {
            /* Declared but not defined here: exported only if relocated
             * against (see g_obj_referenced). */
            if (!g_obj_referenced[gidx]) return false;
            flags = ONYO_SYM_UNDEF | ONYO_SYM_FUNC | ONYO_SYM_GLOBAL;
            sec = -1;
        }
    } else if (s->kind == SYM_GLOBAL_VAR) {
        if (s->allocated) {
            /* is_defined == "has an initializer": the same test
             * gen_finalize() uses to choose .data over .bss. */
            flags = ONYO_SYM_DEFINED | ONYO_SYM_DATA |
                    (s->is_static ? ONYO_SYM_LOCAL : ONYO_SYM_GLOBAL);
            sec = s->is_defined ? ONYO_SEC_DATA : ONYO_SEC_BSS;
            value = s->text_off;
        } else {
            /* A reserved data symbol that was never filled in: this is the
             * vtable of a non-polymorphic class (cpp_class_open() reserves
             * `__vt_Tag` for every class). It owns no storage and nothing
             * references it, so it must not appear as an undefined symbol —
             * the linker would reject the object. */
            if (s->is_defined) return false;
            /* `extern` declaration only: no storage in this object. */
            if (!g_obj_referenced[gidx]) return false;
            flags = ONYO_SYM_UNDEF | ONYO_SYM_DATA | ONYO_SYM_GLOBAL;
            sec = -1;
        }
    } else {
        return false;
    }
    uint32_t name_off = strtab_add(strtab, s->name);
    if (g_opts.verbose)
        fprintf(stderr, "obj: sym[%d] %s flags=0x%x sec=%d value=0x%llx\n",
                (int)(syms->size / ONYO_SYMBOL_SIZE), s->name, flags,
                (int)sec, (unsigned long long)value);
    push_symbol(syms, name_off, flags, sec, value);
    return true;
}

int obj_emit(const char *path) {
    cc_buf_t strtab, syms, relocs, hdr;
    uint32_t sec_name[OBJ_N_SECS];
    int32_t sym_map[CC_MAX_GLOBALS];
    int rc = 1;

    cc_buf_init(&strtab);
    cc_buf_init(&syms);
    cc_buf_init(&relocs);
    cc_buf_init(&hdr);

    /* Mark every symbol reached by a relocation: only those need an
     * UNDEF entry when they are not defined in this object. */
    memset(g_obj_referenced, 0, sizeof(g_obj_referenced));
    for (int i = 0; i < gen_addr_fixup_count(); i++) {
        int sym_idx;
        uint32_t patch_lui, patch_addi;
        int64_t extra_off;
        gen_addr_fixup_get(i, &sym_idx, &patch_lui, &patch_addi, &extra_off);
        if (sym_idx >= 0 && sym_idx < CC_MAX_GLOBALS) g_obj_referenced[sym_idx] = true;
    }
    for (int i = 0; i < gen_data_sym_fixup_count(); i++) {
        int sym_idx;
        uint32_t data_off;
        gen_data_sym_fixup_get(i, &sym_idx, &data_off);
        if (sym_idx >= 0 && sym_idx < CC_MAX_GLOBALS) g_obj_referenced[sym_idx] = true;
    }

    /* String table: leading NUL keeps offset 0 (empty name) valid. */
    cc_buf_push8(&strtab, 0);
    for (int i = 0; i < OBJ_N_SECS; i++) sec_name[i] = strtab_add(&strtab, k_secs[i].name);

    /* Symbols: the four section symbols first (also reloc targets for
     * rodata references), then every function/global object. */
    for (int i = 0; i < OBJ_N_SECS; i++)
        push_symbol(&syms, sec_name[i],
                    ONYO_SYM_DEFINED | ONYO_SYM_LOCAL | ONYO_SYM_DATA, i, 0);
    for (int i = 0; i < g_n_globals; i++) {
        sym_map[i] = -1;
        if (push_global_symbol(&syms, &strtab, i))
            sym_map[i] = (int32_t)(syms.size / ONYO_SYMBOL_SIZE) - 1;
    }

    /* Relocations, derived one-to-one from the compiler fixup tables. */
    if (obj_write_relocs(&relocs, sym_map, g_n_globals) != 0) goto done;

    /* Section bytes start right after the string table, in section order. */
    uint64_t text_off = (uint64_t)(ONYO_HDR_SIZE + OBJ_N_SECS * ONYO_SECTION_HDR_SIZE)
                      + syms.size + relocs.size + strtab.size;
    uint64_t rodata_off = text_off + g_text.size;
    uint64_t data_off = rodata_off + g_rodata.size;

    cc_buf_push32(&hdr, ONYO_MAGIC);
    cc_buf_push32(&hdr, ONYO_VERSION_1);
    cc_buf_push32(&hdr, 0);            /* flags: RING1 is a link-time choice */
    cc_buf_push32(&hdr, OBJ_N_SECS);
    cc_buf_push32(&hdr, (uint32_t)(syms.size / ONYO_SYMBOL_SIZE));
    cc_buf_push32(&hdr, (uint32_t)(relocs.size / ONYO_RELOC_SIZE));
    cc_buf_push32(&hdr, (uint32_t)strtab.size);
    cc_buf_push32(&hdr, ONYO_NO_ENTRY);  /* entry symbol is resolved by onyx-ld */
    for (int i = 0; i < 32; i++) cc_buf_push8(&hdr, 0);   /* reserved */

    uint64_t sec_size[OBJ_N_SECS] = { g_text.size, g_rodata.size, g_data.size, g_bss_size };
    uint64_t sec_data_off[OBJ_N_SECS] = { text_off, rodata_off, data_off, 0 };
    for (int i = 0; i < OBJ_N_SECS; i++) {
        /* name_off, type, flags, align, size (u64), data_off (u64), 8 pad */
        cc_buf_push32(&hdr, sec_name[i]);
        cc_buf_push32(&hdr, (uint32_t)i);
        cc_buf_push32(&hdr, k_secs[i].flags);
        cc_buf_push32(&hdr, k_secs[i].align);
        cc_buf_push64(&hdr, sec_size[i]);
        cc_buf_push64(&hdr, sec_data_off[i]);
        cc_buf_push64(&hdr, 0);   /* reserved: entry size is 40 bytes */
    }

    FILE *f = fopen(path, "wb");
    if (!f) {
        cc_error("obj: cannot open %s for writing", path);
        goto done;
    }
    fwrite(hdr.data, 1, hdr.size, f);
    if (syms.size)   fwrite(syms.data, 1, syms.size, f);
    if (relocs.size) fwrite(relocs.data, 1, relocs.size, f);
    if (strtab.size) fwrite(strtab.data, 1, strtab.size, f);
    if (g_text.size)   fwrite(g_text.data, 1, g_text.size, f);
    if (g_rodata.size) fwrite(g_rodata.data, 1, g_rodata.size, f);
    if (g_data.size)   fwrite(g_data.data, 1, g_data.size, f);
    if (fclose(f) != 0) {   /* catches buffered write errors */
        cc_error("obj: write error on %s", path);
        goto done;
    }

    if (g_opts.verbose) {
        fprintf(stderr, "obj: %s -> %s text=%llu rodata=%llu data=%llu bss=%llu "
                "syms=%d relocs=%d\n",
                g_opts.input ? g_opts.input : "<stdin>", path,
                (unsigned long long)g_text.size,
                (unsigned long long)g_rodata.size,
                (unsigned long long)g_data.size,
                (unsigned long long)g_bss_size,
                (int)(syms.size / ONYO_SYMBOL_SIZE),
                (int)(relocs.size / ONYO_RELOC_SIZE));
    }
    rc = 0;

done:
    cc_buf_free(&hdr);
    cc_buf_free(&relocs);
    cc_buf_free(&syms);
    cc_buf_free(&strtab);
    return rc;
}
