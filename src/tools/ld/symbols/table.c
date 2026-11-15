/* Global binding: strong wins; the first weak definition is retained. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../ld_internal.h"

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
                sym_t *previous = &g_global_syms[existing];
                if (s->flags & ONYO_SYM_WEAK) continue;
                if (previous->flags & ONYO_SYM_WEAK) { *previous = *s; continue; }
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
            if ((s->flags & (ONYO_SYM_DEFINED | ONYO_SYM_WEAK)) || !s->name[0]) continue;
            if (ld_find_global_sym(s->name) < 0) {
                fprintf(stderr, "onyx-ld: undefined symbol '%s' (referenced from %s)\n",
                        s->name, obj->name);
                return -1;
            }
        }
    }
    return 0;
}

bool ld_resolve_symbol(const sym_t *symbol, uint64_t *address) {
    const sym_t *target = symbol;
    if ((symbol->flags & ONYO_SYM_GLOBAL) && !(symbol->flags & ONYO_SYM_LOCAL)) {
        int index = ld_find_global_sym(symbol->name);
        if (index >= 0) target = &g_global_syms[index];
        else if ((symbol->flags & ONYO_SYM_WEAK) && !(symbol->flags & ONYO_SYM_DEFINED)) {
            *address = 0;
            return true;
        } else return false;
    }
    return ld_sym_vaddr(target, address);
}
