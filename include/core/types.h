/*
 * types.h — type system for OnyxCC.
 *
 * C type representation. C++ adds namespaces/classes on top in cpp.h.
 */
#ifndef CC_TYPES_H
#define CC_TYPES_H

#include "cc.h"

typedef enum {
    TY_VOID,
    TY_BOOL,
    TY_CHAR,
    TY_SCHAR,
    TY_UCHAR,
    TY_SHORT,
    TY_USHORT,
    TY_INT,
    TY_UINT,
    TY_LONG,
    TY_ULONG,
    TY_LLONG,
    TY_ULLONG,
    TY_FLOAT,
    TY_DOUBLE,
    TY_LDOUBLE,
    TY_PTR,
    TY_ARRAY,
    TY_FUNC,
    TY_STRUCT,
    TY_UNION,
    TY_ENUM,
} type_kind_t;

typedef struct type type_t;
typedef struct struct_field struct_field_t;

struct struct_field {
    char name[CC_MAX_IDENT];
    type_t *type;
    uint64_t offset;
    uint32_t bit_width;   /* 0 = not a bitfield */
    /* Anonymous unions: */
    bool is_anon;
};

typedef struct {
    char name[CC_MAX_IDENT];
    type_t *type;
    bool is_used;        /* marked as referenced for varargs detection */
} func_param_t;

struct type {
    type_kind_t kind;
    bool is_const;
    bool is_volatile;
    bool is_restrict;
    bool is_static;
    bool is_extern;
    bool is_inline;
    bool is_typedef;
    bool is_register;
    bool is_auto;       /* C23 auto, treated as int in MVP */
    /* For pointers: */
    type_t *base;
    /* For arrays: */
    uint64_t length;       /* 0 = unsized */
    bool is_incomplete;    /* e.g. int x[] in a struct field */
    /* For functions: */
    func_param_t *params; /* lazy arena payload, capacity CC_MAX_FUNC_PARAMS */
    int nparams;
    bool is_varargs;
    type_t *ret;
    /* For structs/unions: */
    char tag[CC_MAX_IDENT];
    struct_field_t *fields; /* lazy arena payload, capacity CC_MAX_STRUCT_FIELDS */
    int nfields;
    uint64_t size;
    uint64_t align;
    bool is_complete;
    /* For enums: */
    /* (enum stores its underlying integer type in `base`) */
};

/* Singleton primitive types (initialized in types_init). */
extern type_t ty_void, ty_bool, ty_char, ty_schar, ty_uchar,
            ty_short, ty_ushort, ty_int, ty_uint,
            ty_long, ty_ulong, ty_llong, ty_ullong,
            ty_float, ty_double, ty_ldouble;

void types_init(cc_arena_t *arena);

type_t *type_make_function(type_t *ret);
void type_ensure_params(type_t *type);
void type_ensure_fields(type_t *type);
type_t *type_make_ptr(type_t *base);
type_t *type_make_array(type_t *base, uint64_t length);
type_t *type_make_const(type_t *base);
type_t *type_dup(type_t *src);

bool type_is_integer(type_t *t);
bool type_is_signed(type_t *t);
bool type_is_unsigned(type_t *t);
bool type_is_pointer(type_t *t);
bool type_is_arith(type_t *t);
bool type_is_scalar(type_t *t);
uint64_t type_sizeof(type_t *t);
uint64_t type_alignof(type_t *t);
type_t *type_unqual(type_t *t);
type_t *type_decay(type_t *t);     /* array → pointer, function → ptr-to-fn */
type_t *type_promote(type_t *t);   /* integer promotions */
type_t *type_common(type_t *a, type_t *b);  /* usual arithmetic conversions */

/* tag (struct/union/enum) storage. */
typedef struct {
    char tag[CC_MAX_IDENT];
    type_t *type;
} tag_entry_t;

void tags_init(void);
type_t *tags_lookup(const char *name);
void tags_install(const char *name, type_t *t);

#endif /* CC_TYPES_H */
