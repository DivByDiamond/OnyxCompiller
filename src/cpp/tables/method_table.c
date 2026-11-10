/*
 * method_table.c — method groups and arity-based overload resolution
 * (spec item 5/8, Phase B).
 *
 * A group is one (class, name) pair — the id that travels in
 * val_t.cpp_group as the parse_postfix marker. Free-function
 * overloading is deliberately out of scope for Phase B (free functions
 * keep raw C names).
 */
#include "core/compat.h"

#include "cpp/cpp_front.h"

cpp_method_t g_methods[CPP_MAX_METHODS];
int g_n_methods = 0;
cpp_group_t g_groups[CPP_MAX_GROUPS];
int g_n_groups = 0;

/* Group of one exact (owner, name) pair, or -1. */
static int group_own(int owner, const char *name) {
    for (int i = 0; i < g_n_groups; i++)
        if (g_groups[i].owner == owner &&
            strcmp(g_groups[i].name, name) == 0)
            return i;
    return -1;
}

int cpp_group_find(int ci, const char *name) {
    /* Walk the base chain: a derived object exposes base methods (the
     * base subobject sits at offset 0, so one hidden this works). */
    for (int c = ci; c >= 0; c = g_classes[c].base_idx) {
        int g = group_own(c, name);
        if (g >= 0) return g;
    }
    return -1;
}

int cpp_method_resolve_gid(int gid, int nargs) {
    if (gid < 0 || gid >= g_n_groups) return -1;
    for (int i = 0; i < g_n_methods; i++)
        if (g_methods[i].group == gid && g_methods[i].arity == nargs)
            return i;
    return -1;
}

void cpp_method_add(int owner, const char *name, int arity, int access,
                    type_t *ftype, const char *mangled, bool defining,
                    bool is_virtual) {
    if (g_n_methods >= CPP_MAX_METHODS) cc_fatal("too many methods");
    int g = group_own(owner, name);
    if (g < 0) {
        if (g_n_groups >= CPP_MAX_GROUPS) cc_fatal("too many method groups");
        g = g_n_groups++;
        g_groups[g].owner = owner;
        strncpy(g_groups[g].name, name, CC_MAX_IDENT - 1);
    }
    /* Redeclaration of the same signature reuses the entry (the symbol
     * stays whatever the first declaration installed); only compiling a
     * second body is an error. */
    int existing = cpp_method_resolve_gid(g, arity);
    if (existing >= 0) {
        if (defining && g_methods[existing].defined)
            cc_error("redefinition of '%s'", name);
        if (defining) g_methods[existing].defined = true;
        if (is_virtual) g_methods[existing].is_virtual = true;
        return;
    }
    if (g_n_methods >= CPP_MAX_METHODS) cc_fatal("too many methods");
    cpp_method_t *m = &g_methods[g_n_methods++];
    memset(m, 0, sizeof(*m));
    m->group = g;
    m->arity = arity;
    m->access = access;
    m->defined = defining;
    m->ftype = ftype;
    m->is_virtual = is_virtual;
    m->slot = -1;
    if (is_virtual) {
        /* Overriding a base declaration reuses its slot; otherwise a new
         * slot is appended after every slot already visible in the chain. */
        int slot = -1;
        for (int c = owner; c >= 0 && slot < 0; c = g_classes[c].base_idx) {
            for (int i = 0; i < g_n_methods - 1; i++) {
                if (!g_methods[i].is_virtual) continue;
                if (g_groups[g_methods[i].group].owner != c) continue;
                if (g_methods[i].arity != arity) continue;
                if (strcmp(g_groups[g_methods[i].group].name, name) != 0) continue;
                slot = g_methods[i].slot;
                break;
            }
        }
        if (slot < 0) {
            int count = 0;
            for (int c = owner; c >= 0; c = g_classes[c].base_idx)
                for (int i = 0; i < g_n_methods - 1; i++)
                    if (g_methods[i].is_virtual &&
                        g_groups[g_methods[i].group].owner == c &&
                        g_methods[i].slot + 1 > count)
                        count = g_methods[i].slot + 1;
            slot = count;
        }
        m->slot = slot;
        g_classes[owner].polymorphic = true;
    }
    strncpy(m->mangled, mangled, CC_MAX_IDENT - 1);
}

int cpp_method_for_slot(int ci, int slot) {
    for (int c = ci; c >= 0; c = g_classes[c].base_idx) {
        for (int i = 0; i < g_n_methods; i++) {
            if (!g_methods[i].is_virtual || g_methods[i].slot != slot)
                continue;
            if (g_groups[g_methods[i].group].owner != c) continue;
            return i;
        }
    }
    return -1;
}
