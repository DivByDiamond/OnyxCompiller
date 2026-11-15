/* Whole-object archive selection; no symbol index or section GC. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../ld_internal.h"

static bool exported(const sym_t *s) {
    return (s->flags & ONYO_SYM_DEFINED) && (s->flags & ONYO_SYM_GLOBAL) &&
           !(s->flags & ONYO_SYM_LOCAL) && s->name[0];
}

static bool defined(const char *name) {
    for (int i = 0; i < g_n_objs; i++)
        for (int j = 0; j < g_objs[i].n_syms; j++) {
            sym_t *s = &g_objs[i].syms[j];
            if (exported(s) && !strcmp(name, s->name)) return true;
        }
    return false;
}

static bool needed(const char *name) {
    if (defined(name)) return false;
    if (!strcmp(name, g_ld_opts.entry_sym)) return true;
    for (int i = 0; i < g_n_objs; i++)
        for (int j = 0; j < g_objs[i].n_syms; j++) {
            sym_t *s = &g_objs[i].syms[j];
            if (!(s->flags & (ONYO_SYM_DEFINED | ONYO_SYM_WEAK)) &&
                !strcmp(name, s->name)) return true;
        }
    return false;
}

bool ld_archive_needed(const obj_t *obj) {
    for (int i = 0; i < obj->n_syms; i++)
        if (exported(&obj->syms[i]) && needed(obj->syms[i].name)) return true;
    return false;
}

int ld_adopt_object(obj_t *obj, bool copy) {
    if (g_n_objs >= MAX_OBJECTS) {
        fprintf(stderr, "onyx-ld: too many input objects (max %d)\n", MAX_OBJECTS);
        return -1;
    }
    if (copy) {
        uint8_t *data = malloc(obj->file_size ? obj->file_size : 1);
        if (!data) { fprintf(stderr, "onyx-ld: out of memory\n"); return -1; }
        memcpy(data, obj->file_data, obj->file_size);
        obj->file_data = data;
        for (int i = 0; i < obj->n_secs; i++)
            if (obj->secs[i].type != ONYO_SEC_BSS)
                obj->secs[i].data = data + obj->secs[i].data_off;
    }
    for (int i = 0; i < obj->n_syms; i++) obj->syms[i].obj_idx = g_n_objs;
    g_objs[g_n_objs++] = *obj;
    return 0;
}

void ld_cleanup(void) {
    for (int i = 0; i < g_n_objs; i++) {
        free(g_objs[i].syms);
        free(g_objs[i].relocs);
        free(g_objs[i].file_data);
    }
    free(g_global_syms);
    free(g_out_text);
    free(g_out_rodata);
    free(g_out_data);
}
