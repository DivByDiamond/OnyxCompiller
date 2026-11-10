/*
 * link.c — section layout, global symbol table and RISC-V relocations.
 *
 * Sections are concatenated per type; each object section records its
 * offset inside the merged section so relocations can patch the final
 * image directly instead of patching objects beforehand.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ld_internal.h"

/* ── Linker state ───────────────────────────────────────────────────── */

obj_t g_objs[MAX_OBJECTS];
int   g_n_objs = 0;

uint8_t *g_out_text = NULL, *g_out_rodata = NULL, *g_out_data = NULL;
size_t   g_out_text_size = 0, g_out_rodata_size = 0, g_out_data_size = 0;
uint64_t g_out_bss_size = 0;

sym_t *g_global_syms = NULL;
int    g_n_global_syms = 0;
uint64_t g_text_vaddr = ONYX_LD_TEXT_VADDR;
uint64_t g_rodata_vaddr = 0, g_data_vaddr = 0, g_bss_vaddr = 0;

/* Output buffer and vaddr of a loaded section type (false for .bss). */
static bool out_section(uint32_t type, uint8_t **bytes, uint64_t *vaddr) {
    *bytes = type == ONYO_SEC_TEXT ? g_out_text
           : type == ONYO_SEC_RODATA ? g_out_rodata
           : type == ONYO_SEC_DATA ? g_out_data : NULL;
    *vaddr = type == ONYO_SEC_TEXT ? g_text_vaddr
           : type == ONYO_SEC_RODATA ? g_rodata_vaddr
           : type == ONYO_SEC_DATA ? g_data_vaddr : 0;
    return type <= ONYO_SEC_DATA;
}

/* ── Layout ─────────────────────────────────────────────────────────── */

int onyx_ld_merge_sections(void) {
    uint64_t size[4] = { 0, 0, 0, 0 };   /* merged bytes per ONYO_SEC_* */

    /* Pass 1: place sections and record merged offsets, .bss included. */
    for (int i = 0; i < g_n_objs; i++) {
        obj_t *obj = &g_objs[i];
        for (int j = 0; j < obj->n_secs; j++) {
            sec_t *s = &obj->secs[j];
            uint64_t align = s->align ? s->align : 1;
            if (s->type > ONYO_SEC_BSS || (align & (align - 1))) {
                fprintf(stderr, "onyx-ld: %s: malformed section %d\n", obj->name, j);
                return -1;
            }
            size[s->type] = (size[s->type] + align - 1) & ~(align - 1);
            s->data_off = size[s->type];
            size[s->type] += s->size;
        }
    }

    if ((size[ONYO_SEC_TEXT] &&
         !(g_out_text = (uint8_t *)calloc(1, (size_t)size[ONYO_SEC_TEXT]))) ||
        (size[ONYO_SEC_RODATA] &&
         !(g_out_rodata = (uint8_t *)calloc(1, (size_t)size[ONYO_SEC_RODATA]))) ||
        (size[ONYO_SEC_DATA] &&
         !(g_out_data = (uint8_t *)calloc(1, (size_t)size[ONYO_SEC_DATA])))) {
        fprintf(stderr, "onyx-ld: out of memory for output sections\n");
        return -1;
    }

    /* Pass 2: copy section bytes. .bss is zeroed by the loader. */
    for (int i = 0; i < g_n_objs; i++) {
        obj_t *obj = &g_objs[i];
        for (int j = 0; j < obj->n_secs; j++) {
            sec_t *s = &obj->secs[j];
            uint8_t *base; uint64_t vaddr;
            if (s->size == 0 || !s->data || !out_section(s->type, &base, &vaddr)) continue;
            memcpy(base + s->data_off, s->data, (size_t)s->size);
        }
    }

    g_out_text_size = (size_t)size[ONYO_SEC_TEXT];
    g_out_rodata_size = (size_t)size[ONYO_SEC_RODATA];
    g_out_data_size = (size_t)size[ONYO_SEC_DATA];
    g_out_bss_size = size[ONYO_SEC_BSS];
    return 0;
}

void onyx_ld_compute_addresses(void) {
    g_text_vaddr   = ONYX_LD_TEXT_VADDR;
    g_rodata_vaddr = (g_text_vaddr + g_out_text_size + 15) & ~(uint64_t)15;
    g_data_vaddr   = (g_rodata_vaddr + g_out_rodata_size + 15) & ~(uint64_t)15;
    g_bss_vaddr    = (g_data_vaddr + g_out_data_size + 15) & ~(uint64_t)15;
}

/* ── Symbol table ───────────────────────────────────────────────────── */

int ld_find_global_sym(const char *name) {
    for (int i = 0; i < g_n_global_syms; i++) {
        if (strcmp(g_global_syms[i].name, name) == 0) return i;
    }
    return -1;
}

/* Absolute address of a symbol defined in `sym->obj_idx`: the section
 * base plus the section's merged offset plus the symbol value. */
bool ld_sym_vaddr(const sym_t *sym, uint64_t *out_vaddr) {
    if (sym->section_idx < 0 || sym->obj_idx < 0 || sym->obj_idx >= g_n_objs) return false;
    const obj_t *obj = &g_objs[sym->obj_idx];
    if (sym->section_idx >= obj->n_secs) return false;
    const sec_t *sec = &obj->secs[sym->section_idx];
    uint64_t base;
    switch (sec->type) {
        case ONYO_SEC_TEXT:   base = g_text_vaddr;   break;
        case ONYO_SEC_RODATA: base = g_rodata_vaddr; break;
        case ONYO_SEC_DATA:   base = g_data_vaddr;   break;
        case ONYO_SEC_BSS:    base = g_bss_vaddr;    break;
        default: return false;
    }
    *out_vaddr = base + sec->data_off + sym->value;
    return true;
}

int onyx_ld_build_symbol_table(void) {
    if (!g_global_syms &&
        !(g_global_syms = (sym_t *)calloc(MAX_SYMS_TOTAL, sizeof(sym_t)))) {
        fprintf(stderr, "onyx-ld: OOM allocating global symbol table\n");
        return -1;
    }
    /* Only DEFINED GLOBAL symbols enter the shared namespace. LOCAL and
     * section symbols resolve inside their own object during relocation. */
    for (int i = 0; i < g_n_objs; i++) {
        obj_t *obj = &g_objs[i];
        for (int j = 0; j < obj->n_syms; j++) {
            sym_t *s = &obj->syms[j];
            const uint32_t wanted = ONYO_SYM_DEFINED | ONYO_SYM_GLOBAL;
            if ((s->flags & (wanted | ONYO_SYM_LOCAL)) != wanted || !s->name[0]) continue;
            int existing = ld_find_global_sym(s->name);
            if (existing >= 0) {
                fprintf(stderr, "onyx-ld: duplicate symbol '%s' (in %s and %s)\n",
                        s->name, g_objs[g_global_syms[existing].obj_idx].name, obj->name);
                return -1;
            }
            if (g_n_global_syms >= MAX_SYMS_TOTAL) {
                fprintf(stderr, "onyx-ld: too many symbols\n"); return -1;
            }
            g_global_syms[g_n_global_syms++] = *s;
        }
    }
    for (int i = 0; i < g_n_objs; i++) {   /* every undefined name must resolve */
        obj_t *obj = &g_objs[i];
        for (int j = 0; j < obj->n_syms; j++) {
            sym_t *s = &obj->syms[j];
            if ((s->flags & ONYO_SYM_DEFINED) || !s->name[0]) continue;
            if (ld_find_global_sym(s->name) < 0) {
                fprintf(stderr, "onyx-ld: undefined symbol '%s' (referenced from %s)\n",
                        s->name, obj->name);
                return -1;
            }
        }
    }
    return 0;
}

/* ── Relocations ────────────────────────────────────────────────────── */

int onyx_ld_apply_relocations(void) {
    for (int i = 0; i < g_n_objs; i++) {
        obj_t *obj = &g_objs[i];
        for (int j = 0; j < obj->n_relocs; j++) {
            reloc_t *r = &obj->relocs[j];
            if (r->section_idx >= (uint32_t)obj->n_secs) {
                fprintf(stderr, "onyx-ld: %s: bad reloc section %u\n", obj->name, r->section_idx);
                return -1;
            }
            sec_t *sec = &obj->secs[r->section_idx];
            uint8_t *merged; uint64_t base_vaddr;
            if (!out_section(sec->type, &merged, &base_vaddr)) {
                fprintf(stderr, sec->type == ONYO_SEC_BSS
                        ? "onyx-ld: cannot have reloc in BSS\n"
                        : "onyx-ld: bad section type in reloc\n");
                return -1;
            }
            if (r->offset > sec->size ||
                sec->size - r->offset < ld_reloc_width(r->type) ||
                r->sym_idx >= (uint32_t)obj->n_syms) {
                fprintf(stderr, "onyx-ld: %s: malformed reloc %d\n", obj->name, j);
                return -1;
            }
            /* DEFINED targets — LOCAL, section and GLOBAL symbols — are
             * resolved inside their own object. Only UNDEF targets are
             * looked up by name in the global table. */
            sym_t *target = &obj->syms[r->sym_idx];
            if (!(target->flags & ONYO_SYM_DEFINED)) {
                int gi = ld_find_global_sym(target->name);
                if (gi < 0) {
                    fprintf(stderr, "onyx-ld: undefined symbol '%s' (referenced from %s)\n",
                            target->name, obj->name);
                    return -1;
                }
                target = &g_global_syms[gi];
            }
            uint64_t S;
            if (!ld_sym_vaddr(target, &S)) {
                fprintf(stderr, "onyx-ld: %s: symbol '%s' has an invalid section\n",
                        obj->name, target->name);
                return -1;
            }
            uint64_t A = (uint64_t)(int64_t)r->addend;
            uint64_t P = base_vaddr + sec->data_off + r->offset;
            uint8_t *at = merged + sec->data_off + r->offset;
            int64_t delta = (int64_t)(S + A) - (int64_t)P;
            /* PC-relative types 6..8 use delta; absolute ones use S + A. */
            uint64_t V = r->type >= R_ONYO_PCREL_HI20 ? (uint64_t)delta : S + A;
            switch (r->type) {
                case R_ONYO_64:      ld_wr_u64(at, S + A); break;
                case R_ONYO_32:      ld_wr_u32(at, (uint32_t)(S + A)); break;
                case R_ONYO_HI20: case R_ONYO_PCREL_HI20:
                    ld_patch_u(at, (uint32_t)((V + 0x800) & 0xFFFFF000u) >> 12);
                    break;
                case R_ONYO_LO12_I: case R_ONYO_PCREL_LO12_I:
                    ld_patch_i(at, (int32_t)(V & 0xFFF));
                    break;
                case R_ONYO_LO12_S: case R_ONYO_PCREL_LO12_S:
                    ld_patch_s(at, (int32_t)(V & 0xFFF));
                    break;
                case R_ONYO_JAL:
                    if (delta > (1 << 20) - 1 || delta < -(1 << 20)) {
                        fprintf(stderr, "onyx-ld: jal delta %lld out of range for '%s'\n",
                                (long long)delta, target->name);
                        return -1;
                    }
                    ld_patch_j(at, (int32_t)delta);
                    break;
                case R_ONYO_BRANCH:
                    if (delta > 4095 || delta < -4096) {
                        fprintf(stderr, "onyx-ld: branch delta %lld out of range for '%s'\n",
                                (long long)delta, target->name);
                        return -1;
                    }
                    ld_patch_b(at, (int32_t)delta);
                    break;
                default:
                    fprintf(stderr, "onyx-ld: unknown reloc type %u\n", r->type);
                    return -1;
            }
        }
    }
    return 0;
}
