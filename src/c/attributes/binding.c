/* Declaration binding persists across prototypes and definitions. */
#include "core/compat.h"
#include "core/cc.h"
#include "front/ast.h"
#include "back/gen.h"

/* Compare the supported type graph, bounding recursion on hostile input.
 * Aggregate identity is nominal; arrays may complete an unsized declaration. */
static bool compatible(const type_t *a, const type_t *b, unsigned depth) {
    if (a == b) return true;
    if (!a || !b || depth > 64 || a->kind != b->kind || a->is_ref != b->is_ref)
        return false;
    if (a->kind == TY_STRUCT || a->kind == TY_UNION || a->kind == TY_ENUM)
        return false;
    if (a->kind == TY_PTR || a->kind == TY_ARRAY) {
        if (a->kind == TY_ARRAY && a->length && b->length && a->length != b->length)
            return false;
        return compatible(a->base, b->base, depth + 1);
    }
    if (a->kind == TY_FUNC) {
        if (!compatible(a->ret, b->ret, depth + 1)) return false;
        if (!a->has_prototype || !b->has_prototype) {
            const type_t *proto = a->has_prototype ? a : b;
            if (proto->is_varargs) return false;
            for (int i = 0; i < proto->nparams; i++) {
                type_t *t = proto->params[i].type;
                if (t->kind == TY_FLOAT ||
                    (type_is_integer(t) && type_promote(t)->kind != t->kind)) return false;
            }
            return true;
        }
        if (a->nparams != b->nparams || a->is_varargs != b->is_varargs) return false;
        for (int i = 0; i < a->nparams; i++)
            if (!compatible(a->params[i].type, b->params[i].type, depth + 1)) return false;
    }
    return true;
}

/* Keep completion information so subsequent declarations cannot compare
 * against an obsolete unsized array or non-prototype function type. */
static void retain_completion(type_t *a, const type_t *b, unsigned depth) {
    if (a == b || depth > 64) return;
    if (a->kind == TY_ARRAY && !a->length && b->length) {
        a->length = b->length;
        a->is_incomplete = b->is_incomplete;
        a->is_complete = b->is_complete;
        a->size = b->size;
        a->align = b->align;
    }
    if (a->kind == TY_PTR || a->kind == TY_ARRAY)
        retain_completion(a->base, b->base, depth + 1);
    if (a->kind == TY_FUNC) {
        if (!a->has_prototype && b->has_prototype) { *a = *b; return; }
        retain_completion(a->ret, b->ret, depth + 1);
        if (a->has_prototype && b->has_prototype)
            for (int i = 0; i < a->nparams; i++)
                retain_completion(a->params[i].type, b->params[i].type, depth + 1);
    }
}

int gen_bind_global(const char *name, sym_kind_t kind, const decl_t *decl) {
    int index = symtab_install_global(name, kind, decl->type);
    sym_t *symbol = &g_globals[index];
    if ((decl->is_weak || symbol->is_weak) &&
        (symbol->kind != kind || !compatible(symbol->type, decl->type, 0))) {
        cc_error_at(decl->pos.file, decl->pos.line,
                    "incompatible redeclaration of weak symbol '%s'", name);
        return -1;
    }
    if (decl->is_weak || symbol->is_weak)
        retain_completion(symbol->type, decl->type, 0);
    if (decl->is_weak) symbol->is_weak = true;
    if (decl->is_static) symbol->is_static = true;
    if (symbol->is_weak && symbol->is_static)
        cc_error_at(decl->pos.file, decl->pos.line,
                    "weak symbol '%s' must have external linkage", name);
    if (symbol->is_weak && !g_opts.compile_only && !g_opts.syntax_only)
        cc_error_at(decl->pos.file, decl->pos.line,
                    "weak symbol '%s' requires -c and onyx-ld", name);
    return index;
}
