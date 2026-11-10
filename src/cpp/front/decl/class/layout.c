/*
 * decl/class/layout.c — field layout, base flattening, vtable build.
 */
#include "core/compat.h"

#include "core/cc.h"
#include "core/types.h"
#include "front/lexer.h"
#include "front/ast.h"
#include "front/parser_priv.h"
#include "back/gen.h"
#include "arch/riscv64.h"
#include "cpp/cpp_front.h"
#include "cpp/cpp.h"

void cpp_class_append_field(int ci, type_t *ftype, const char *name,
                            int access) {
    cpp_class_t *c = &g_classes[ci];
    type_t *t = c->type;
    type_ensure_fields(t);
    if (t->nfields >= CC_MAX_STRUCT_FIELDS) {
        cc_error("too many fields in '%s'", c->tag);
        return;
    }
    uint64_t sz = type_sizeof(ftype);
    uint64_t al = type_alignof(ftype);
    if (al < 1) al = 1;
    t->size = (t->size + al - 1) & ~(al - 1);
    struct_field_t *f = &t->fields[t->nfields];
    memset(f, 0, sizeof(*f));
    strncpy(f->name, name, CC_MAX_IDENT - 1);
    f->type = ftype;
    f->offset = t->size;
    if (al > t->align) t->align = al;
    t->size += sz;
    int local = t->nfields - c->own_start;
    if (local >= 0 && local < CC_MAX_STRUCT_FIELDS)
        c->field_access[local] = (unsigned char)access;
    t->nfields++;
}

bool cpp_class_set_base(int ci, const char *base_tag) {
    cpp_class_t *c = &g_classes[ci];
    if (c->base_idx >= 0) {
        cc_error("multiple inheritance is not supported yet");
        return false;
    }
    int bi = cpp_class_by_tag(base_tag);
    if (bi < 0) {
        cc_error("unknown base class '%s'", base_tag);
        return false;
    }
    if (bi == ci) {
        cc_error("class '%s' cannot inherit from itself", c->tag);
        return false;
    }
    if (!g_classes[bi].complete) {
        cc_error("base class '%s' is incomplete", base_tag);
        return false;
    }
    type_t *base = g_classes[bi].type;
    type_t *t = c->type;
    type_ensure_fields(t);
    /* Replace the reserved lone vptr slot with the base's flattened
     * layout: the base subobject lives at offset 0 and carries its own
     * vptr field when polymorphic. */
    t->nfields = 0;
    t->size = 0;
    for (int i = 0; i < base->nfields; i++) {
        if (t->nfields >= CC_MAX_STRUCT_FIELDS) {
            cc_error("too many fields in '%s'", c->tag);
            return false;
        }
        t->fields[t->nfields++] = base->fields[i];
    }
    c->base_idx = bi;
    c->own_start = t->nfields;
    t->size = base->size;
    t->align = base->align ? base->align : 1;
    if (g_classes[bi].polymorphic) c->polymorphic = true;
    return true;
}

void cpp_class_finish(int ci) {
    cpp_class_t *c = &g_classes[ci];
    type_t *t = c->type;
    uint64_t al = t->align ? t->align : 1;
    t->size = (t->size + al - 1) & ~(al - 1);
    if (t->size == 0) t->size = 1;     /* distinct addresses for empty classes */
    type_ensure_fields(t);
    t->is_complete = true;
    c->complete = true;
    if (c->polymorphic) {
        int count = 0;
        for (int x = ci; x >= 0; x = g_classes[x].base_idx)
            for (int i = 0; i < g_n_methods; i++)
                if (g_methods[i].is_virtual &&
                    g_groups[g_methods[i].group].owner == x &&
                    g_methods[i].slot + 1 > count)
                    count = g_methods[i].slot + 1;
        if (count > 0 && count <= 64) {
            int syms[64];
            for (int s = 0; s < count; s++) {
                int mi = cpp_method_for_slot(ci, s);
                syms[s] = (mi >= 0)
                    ? symtab_lookup_global(g_methods[mi].mangled) : -1;
            }
            gen_fill_vtable(c->vt_sym, syms, count);
        }
    }
}

/* ── Error recovery ─────────────────────────────────────────────────── */

