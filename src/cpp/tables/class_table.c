/*
 * class_table.c — class records, base chains, member ownership and
 * access control (spec items 7/8, Phase B).
 *
 * The record array is module-global (extern in cpp/cpp_front.h) so the
 * parsers can drive layout directly; this file owns creation, name
 * lookup and every access decision.
 */
#include "core/compat.h"

#include "front/ast.h"
#include "back/gen.h"
#include "cpp/cpp_front.h"
#include "cpp/cpp.h"

cpp_class_t g_classes[CPP_MAX_CLASSES];
int g_n_classes = 0;

/* Enclosing class of the method being compiled; -1 outside methods. */
static int g_method_class = -1;

int cpp_class_open(const char *tag, cpp_access_t def_access) {
    int ci = cpp_class_by_tag(tag);
    if (ci >= 0 && g_classes[ci].complete) {
        /* Redefinition: report, then parse into a fresh record so the
         * body's layout never appends onto the completed type. The new
         * record/type replace the old ones for later lookups. */
        cc_error("redefinition of '%s'", tag);
        ci = -1;
    }
    if (ci < 0) {
        if (g_n_classes >= CPP_MAX_CLASSES) cc_fatal("too many classes");
        ci = g_n_classes++;
        cpp_class_t *c = &g_classes[ci];
        memset(c, 0, sizeof(*c));
        strncpy(c->tag, tag, CC_MAX_IDENT - 1);
        c->base_idx = -1;
        c->default_access = def_access;
        /* Storage: reuse an incomplete tag type (forward declaration),
         * else create one. Both installs are newest-wins. */
        type_t *t = tags_lookup(tag);
        if (!t || t->is_complete) {
            t = (type_t *)cc_arena_alloc(&g_type_arena, sizeof(type_t), 8);
            memset(t, 0, sizeof(type_t));
            t->kind = TY_STRUCT;
            strncpy(t->tag, tag, CC_MAX_IDENT - 1);
            t->align = 1;
        }
        c->type = t;
        /* Every class reserves the vptr slot at offset 0 (Phase D).
         * Non-polymorphic classes never read it; reserving it up front
         * keeps field indices stable when a virtual method is declared
         * later in the body and lets bases be flattened verbatim. */
        type_ensure_fields(t);
        memset(&t->fields[0], 0, sizeof(t->fields[0]));
        strncpy(t->fields[0].name, "__vptr", CC_MAX_IDENT - 1);
        t->fields[0].type = type_make_ptr(&ty_void);
        t->fields[0].offset = 0;
        t->nfields = 1;
        t->size = 8;
        t->align = 8;
        c->own_start = 1;
        c->vt_sym = -1;
        if (tag[0]) {
            char vtname[CC_MAX_IDENT];
            snprintf(vtname, sizeof(vtname), "__vt_%s", tag);
            c->vt_sym = gen_reserve_data_symbol(vtname);
        }
        if (tag[0]) {
            tags_install(tag, t);
            /* NOTE: no global typedef install here. A typedef leaks into
             * every later C file of the same invocation, where a local
             * named like the class (e.g. `tm` in libonyxc time.c) would
             * start parsing as a cast. Class names resolve through
             * cpp_type_by_name() only while g_cpp_mode is set. */
        }
    }
    return ci;
}

int cpp_class_by_tag(const char *tag) {
    if (!tag || !tag[0]) return -1;
    for (int i = g_n_classes - 1; i >= 0; i--)   /* newest wins */
        if (strcmp(g_classes[i].tag, tag) == 0) return i;
    return -1;
}

type_t *cpp_type_by_name(const char *name) {
    int ci = cpp_class_by_tag(name);
    return ci >= 0 ? g_classes[ci].type : NULL;
}

int cpp_class_field(int ci, const char *name) {
    if (ci < 0) return -1;
    type_t *t = g_classes[ci].type;
    if (!t) return -1;
    for (int i = 0; i < t->nfields; i++)
        if (strcmp(t->fields[i].name, name) == 0) return i;
    return -1;
}

/*
 * Owning record for absolute field index `fi`: inherited fields live in
 * the leading [0, own_start) prefix of every derived type (first-base
 * flattening copies them verbatim), so walking up while fi is below
 * own_start finds the declaring class and fi stays valid there.
 */
static void field_owner(int ci, int fi, int *owner_out, int *local_out) {
    int c = ci;
    while (c >= 0 && fi < g_classes[c].own_start) c = g_classes[c].base_idx;
    *owner_out = c;
    *local_out = (c >= 0) ? fi - g_classes[c].own_start : -1;
}

bool cpp_access_allows(int ctx_ci, int owner_ci, int acc) {
    if (acc == CPP_ACC_PUBLIC) return true;
    if (ctx_ci >= 0 && ctx_ci == owner_ci) return true;
    if (acc == CPP_ACC_PROTECTED && ctx_ci >= 0) {
        /* Protected: the declaring class must be a base of the context.
         * The chain is acyclic by construction (a base must already be
         * complete, and self-derivation is rejected at parse time). */
        for (int c = ctx_ci; c >= 0; c = g_classes[c].base_idx)
            if (c == owner_ci) return true;
    }
    return false;
}

const char *cpp_access_str(int acc) {
    switch (acc) {
        case CPP_ACC_PRIVATE:   return "private";
        case CPP_ACC_PROTECTED: return "protected";
        default:                return "public";
    }
}

/*
 * Access check for `st.member` (spec item 7): emits
 * "'m' is private in 'C'" on denial. Returns whether the access is
 * allowed — gen.c's field seam ignores the result (the diagnostic is
 * the effect), cpp_try_primary uses it to decide whether to synthesize
 * a bare-member reference.
 */
bool cpp_check_member_access(lexer_t *lx, const char *mname, type_t *st) {
    if (!st || !st->tag[0]) return true;      /* not a class: C struct */
    int ci = cpp_class_by_tag(st->tag);
    if (ci < 0) return true;                  /* tag never opened as class */
    int fi = cpp_class_field(ci, mname);
    if (fi < 0) return true;                  /* method: resolved at call */
    int owner = -1, local = -1;
    field_owner(ci, fi, &owner, &local);
    if (owner < 0 || local < 0) return true;
    int acc = g_classes[owner].field_access[local];
    if (cpp_access_allows(g_method_class, owner, acc)) return true;
    cc_error_at(lx->cur.pos.file, lx->cur.pos.line, "'%s' is %s in '%s'",
                mname, cpp_access_str(acc), g_classes[owner].tag);
    return false;
}

int cpp_method_class(void) { return g_method_class; }

void cpp_set_method_class(int ci) { g_method_class = ci; }
