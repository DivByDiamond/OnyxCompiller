/*
 * types.c — type system implementation.
 *
 * Type objects are arena-allocated; the parser never frees them.
 * Primitives are singletons, all derived types go through the arena.
 */
#include "core/compat.h"

#include "core/cc.h"
#include "core/types.h"

type_t ty_void, ty_bool, ty_char, ty_schar, ty_uchar,
       ty_short, ty_ushort, ty_int, ty_uint,
       ty_long, ty_ulong, ty_llong, ty_ullong,
       ty_float, ty_double, ty_ldouble;

static cc_arena_t *g_type_arena;

void types_init(cc_arena_t *arena) {
    g_type_arena = arena;

    memset(&ty_void, 0, sizeof(ty_void));
    ty_void.kind = TY_VOID;
    ty_void.size = 0;
    ty_void.align = 1;
    ty_void.is_complete = true;

    /* On RISC-V LP64: char=1, short=2, int=4, long=8, long long=8,
     * float=4, double=8, long double=16, pointers=8. */
    ty_bool.kind  = TY_BOOL;   ty_bool.size  = 1; ty_bool.align  = 1; ty_bool.is_complete = true;
    ty_char.kind  = TY_CHAR;   ty_char.size  = 1; ty_char.align  = 1; ty_char.is_complete = true;
    ty_schar.kind = TY_SCHAR;  ty_schar.size = 1; ty_schar.align = 1; ty_schar.is_complete = true;
    ty_uchar.kind = TY_UCHAR;  ty_uchar.size = 1; ty_uchar.align = 1; ty_uchar.is_complete = true;
    ty_short.kind = TY_SHORT;  ty_short.size = 2; ty_short.align = 2; ty_short.is_complete = true;
    ty_ushort.kind= TY_USHORT; ty_ushort.size= 2; ty_ushort.align= 2; ty_ushort.is_complete = true;
    ty_int.kind   = TY_INT;    ty_int.size   = 4; ty_int.align   = 4; ty_int.is_complete = true;
    ty_uint.kind  = TY_UINT;   ty_uint.size  = 4; ty_uint.align  = 4; ty_uint.is_complete = true;
    ty_long.kind  = TY_LONG;   ty_long.size  = 8; ty_long.align  = 8; ty_long.is_complete = true;
    ty_ulong.kind = TY_ULONG;  ty_ulong.size = 8; ty_ulong.align = 8; ty_ulong.is_complete = true;
    ty_llong.kind = TY_LLONG;  ty_llong.size = 8; ty_llong.align = 8; ty_llong.is_complete = true;
    ty_ullong.kind= TY_ULLONG; ty_ullong.size= 8; ty_ullong.align= 8; ty_ullong.is_complete = true;
    ty_float.kind = TY_FLOAT;  ty_float.size = 4; ty_float.align = 4; ty_float.is_complete = true;
    ty_double.kind= TY_DOUBLE; ty_double.size= 8; ty_double.align= 8; ty_double.is_complete = true;
    ty_ldouble.kind=TY_LDOUBLE;ty_ldouble.size=16;ty_ldouble.align=16;ty_ldouble.is_complete= true;
}

static type_t *type_alloc(void) {
    return (type_t *)cc_arena_alloc(g_type_arena, sizeof(type_t), 8);
}

type_t *type_make_function(type_t *ret) {
    type_t *t = type_alloc();
    t->kind = TY_FUNC; t->ret = ret;
    t->size = 8; t->align = 8; t->is_complete = true;
    return t;
}

void type_ensure_params(type_t *t) {
    if (!t->params) t->params = cc_arena_alloc(g_type_arena,
        CC_MAX_FUNC_PARAMS * sizeof(*t->params), 8);
}

void type_ensure_fields(type_t *t) {
    if (!t->fields) t->fields = cc_arena_alloc(g_type_arena,
        CC_MAX_STRUCT_FIELDS * sizeof(*t->fields), 8);
}

type_t *type_make_ptr(type_t *base) {
    type_t *t = type_alloc();
    t->kind = TY_PTR;
    t->base = base;
    t->size = 8;
    t->align = 8;
    t->is_complete = true;
    return t;
}

type_t *type_make_array(type_t *base, uint64_t length) {
    type_t *t = type_alloc();
    t->kind = TY_ARRAY;
    t->base = base;
    t->length = length;
    if (length == 0) {
        t->is_incomplete = true;
        t->size = 0;
    } else {
        t->is_incomplete = false;
        t->size = base->size * length;
    }
    t->align = base->align;
    t->is_complete = (length > 0);
    return t;
}

type_t *type_make_const(type_t *base) {
    type_t *t = type_dup(base);
    t->is_const = true;
    return t;
}

type_t *type_dup(type_t *src) {
    type_t *t = type_alloc();
    *t = *src;
    if (src->params) {
        t->params = cc_arena_alloc(g_type_arena, CC_MAX_FUNC_PARAMS * sizeof(*t->params), 8);
        memcpy(t->params, src->params, CC_MAX_FUNC_PARAMS * sizeof(*t->params));
    }
    if (src->fields) {
        t->fields = cc_arena_alloc(g_type_arena, CC_MAX_STRUCT_FIELDS * sizeof(*t->fields), 8);
        memcpy(t->fields, src->fields, CC_MAX_STRUCT_FIELDS * sizeof(*t->fields));
    }
    return t;
}

bool type_is_integer(type_t *t) {
    switch (t->kind) {
        case TY_BOOL: case TY_CHAR: case TY_SCHAR: case TY_UCHAR:
        case TY_SHORT: case TY_USHORT: case TY_INT: case TY_UINT:
        case TY_LONG: case TY_ULONG: case TY_LLONG: case TY_ULLONG:
        case TY_ENUM:
            return true;
        default:
            return false;
    }
}

bool type_is_signed(type_t *t) {
    switch (t->kind) {
        case TY_CHAR:   return true;   /* plain char is signed on RISC-V */
        case TY_SCHAR:  return true;
        case TY_SHORT:  return true;
        case TY_INT:    return true;
        case TY_LONG:   return true;
        case TY_LLONG:  return true;
        case TY_ENUM:   return true;
        default:        return false;
    }
}

bool type_is_unsigned(type_t *t) {
    switch (t->kind) {
        case TY_BOOL:  return true;
        case TY_UCHAR: return true;
        case TY_USHORT:return true;
        case TY_UINT:  return true;
        case TY_ULONG: return true;
        case TY_ULLONG:return true;
        default:       return false;
    }
}

bool type_is_pointer(type_t *t) {
    return t->kind == TY_PTR;
}

bool type_is_arith(type_t *t) {
    return type_is_integer(t) || t->kind == TY_FLOAT || t->kind == TY_DOUBLE || t->kind == TY_LDOUBLE;
}

bool type_is_scalar(type_t *t) {
    return type_is_arith(t) || type_is_pointer(t);
}

uint64_t type_sizeof(type_t *t) {
    if (!t->is_complete) {
        cc_error("sizeof of incomplete type");
        return 0;
    }
    return t->size;
}

uint64_t type_alignof(type_t *t) {
    return t->align;
}

type_t *type_unqual(type_t *t) {
    while (t->is_const || t->is_volatile) {
        /* For our MVP, we don't physically distinguish qualified vs base;
         * we just clear the flags. */
        type_t *u = type_dup(t);
        u->is_const = false;
        u->is_volatile = false;
        t = u;
    }
    return t;
}

type_t *type_decay(type_t *t) {
    if (t->kind == TY_ARRAY) return type_make_ptr(t->base);
    if (t->kind == TY_FUNC)  return type_make_ptr(t);
    return t;
}

type_t *type_promote(type_t *t) {
    /* Integer promotions: small int types promote to int. */
    switch (t->kind) {
        case TY_BOOL:  case TY_CHAR: case TY_SCHAR: case TY_UCHAR:
        case TY_SHORT: case TY_USHORT:
            return &ty_int;
        default:
            return t;
    }
}

type_t *type_common(type_t *a, type_t *b) {
    a = type_promote(a);
    b = type_promote(b);

    /* If either is float/double, common type is the wider. */
    if (a->kind == TY_LDOUBLE || b->kind == TY_LDOUBLE) return &ty_ldouble;
    if (a->kind == TY_DOUBLE  || b->kind == TY_DOUBLE)  return &ty_double;
    if (a->kind == TY_FLOAT   || b->kind == TY_FLOAT)   return &ty_float;

    /* Both integer. Compute the common type by rank and signedness. */
    /* LP64: a wider signed type represents all values of a narrower
     * unsigned type; unsigned wins when widths are equal. */
    uint64_t sa = a->size, sb = b->size;
    if (sa > sb) return a;
    if (sb > sa) return b;
    if (type_is_signed(a) == type_is_signed(b)) return a;
    /* Same size, different signedness: take the unsigned one. */
    return type_is_unsigned(a) ? a : b;
}

/* ---- Tag storage ----------------------------------------------------- */
static tag_entry_t g_tags[CC_MAX_TAGS];
static int g_n_tags;

void tags_init(void) {
    g_n_tags = 0;
}

type_t *tags_lookup(const char *name) {
    for (int i = g_n_tags - 1; i >= 0; i--) {
        if (strcmp(g_tags[i].tag, name) == 0) return g_tags[i].type;
    }
    return NULL;
}

void tags_install(const char *name, type_t *t) {
    if (g_n_tags >= CC_MAX_TAGS) cc_fatal("too many tags");
    strncpy(g_tags[g_n_tags].tag, name, CC_MAX_IDENT - 1);
    g_tags[g_n_tags].type = t;
    g_n_tags++;
}
