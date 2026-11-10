/*
 * cpp_front.h — internal API of the C++ front end.
 *
 * Shared by the front/tables sources under src/cpp; the gen.c/parse.c
 * contract lives in cpp/cpp.h. Helpers shared by the expr/decl/stmt
 * submodules live in cpp_internal.h (included at the end of this file).
 * Every entry point receives the same lexer as g_lx, except during
 * template instantiation, where parser_set_lexer() swaps it briefly.
 */
#ifndef CC_CPP_FRONT_H
#define CC_CPP_FRONT_H

#include "core/cc.h"
#include "core/types.h"
#include "front/lexer.h"
#include "back/val.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Table capacities (cc_fatal on overflow, mirroring symtab_*). */
#define CPP_MAX_CLASSES 128
#define CPP_MAX_METHODS 256
#define CPP_MAX_GROUPS  128

/*
 * Access levels. CPP_ACC_PUBLIC is 0 so zero-initialized records —
 * every plain C struct field that never goes through this module —
 * keep meaning "public" and no C behavior changes.
 */
typedef enum {
    CPP_ACC_PUBLIC = 0,
    CPP_ACC_PRIVATE = 1,
    CPP_ACC_PROTECTED = 2,
} cpp_access_t;

/* One class/struct record. */
typedef struct {
    char tag[CC_MAX_IDENT];
    type_t *type;                 /* TY_STRUCT storage for the class */
    int base_idx;                 /* parent record, -1 = none */
    int own_start;                /* absolute field index where this
                                   * class's own members begin (the
                                   * inherited prefix length) */
    bool complete;                /* body compiled; forward decls stay
                                   * false so a later definition can
                                   * complete the same record */
    bool is_abstract;             /* a `= 0` member was declared */
    bool polymorphic;             /* has (or inherits) a virtual method */
    int n_virtual;                /* own virtual slots (Phase D) */
    int vt_sym;                   /* global symbol of the vtable, -1 */
    cpp_access_t default_access;  /* `class` -> private, `struct` -> public
                                   * (also the loop's initial level) */
    /* Access level of each OWN field, indexed by (field idx -
     * own_start). Inherited fields read their declaring class's copy —
     * hence cpp_field_access() walks the base chain. */
    unsigned char field_access[CC_MAX_STRUCT_FIELDS];
} cpp_class_t;

/*
 * One overload of a method. A "group" is one (class, name) pair; the
 * marker value flowing through parse_postfix carries the GROUP id and
 * resolution by argument count happens at the call site (spec item 5).
 */
typedef struct {
    int group;                    /* overload group this belongs to */
    int arity;                    /* declared args, EXCLUDING hidden this */
    int access;                   /* cpp_access_t of the declaration */
    bool defined;                 /* a body was compiled (redefinition
                                   * guard for a second definition) */
    bool is_virtual;              /* dispatched through the vtable (D) */
    int slot;                     /* vtable slot when is_virtual */
    type_t *ftype;                /* params[0] = Class *this */
    char mangled[CC_MAX_IDENT];   /* symbol installed by gen_decl */
} cpp_method_t;

/* Group = one method name inside one class. */
typedef struct {
    int owner;                    /* class record idx */
    char name[CC_MAX_IDENT];      /* "~Foo" for destructors */
} cpp_group_t;

/* Module state (defined in the tables). */
extern cpp_class_t g_classes[CPP_MAX_CLASSES];
extern int g_n_classes;
extern cpp_method_t g_methods[CPP_MAX_METHODS];
extern int g_n_methods;
extern cpp_group_t g_groups[CPP_MAX_GROUPS];
extern int g_n_groups;

/* ---- class_table.c --------------------------------------------------- */

/*
 * Open the record for `tag`: reuses an incomplete record (forward
 * declaration / elaborated use), otherwise creates one plus its
 * TY_STRUCT storage and installs tag + typedef so `Foo x;` resolves
 * through the existing type paths. A completed redefinition reports
 * cc_error and is parsed into a fresh record. Returns the record idx.
 */
int cpp_class_open(const char *tag, cpp_access_t def_access);

/* Newest record for `tag` (searches backwards), or -1. */
int cpp_class_by_tag(const char *tag);

/* Absolute index of member `name` in the class's flattened field list,
 * or -1. */
int cpp_class_field(int ci, const char *name);

/*
 * Core access rule: public always; same class always; protected when
 * the declaring class is a base of the context; otherwise denied.
 * ctx < 0 means "outside any member function".
 */
bool cpp_access_allows(int ctx_ci, int owner_ci, int acc);

/* "public" / "private" / "protected" for diagnostics. */
const char *cpp_access_str(int acc);

/* Enclosing class of the method currently being compiled (-1 = none).
 * Set/cleared by the method parser around gen_decl. */
int cpp_method_class(void);
void cpp_set_method_class(int ci);

/* ---- method_table.c -------------------------------------------------- */

/*
 * Register (or redeclare) a method. A second declaration with the same
 * (class, name, arity) reuses the entry; defining an already-defined
 * entry reports cc_error. `mangled` must be built BEFORE the hidden
 * this parameter is inserted (args come from params[0..arity-1]).
 * Virtual methods inherit their vtable slot from a base declaration
 * with the same signature, else append a new slot (Phase D).
 */
void cpp_method_add(int owner, const char *name, int arity, int access,
                    type_t *ftype, const char *mangled, bool defining,
                    bool is_virtual);

/* Group id for (class, name) walking the base chain, or -1. */
int cpp_group_find(int ci, const char *name);

/* Method index in group `gid` taking exactly `nargs` args, or -1. */
int cpp_method_resolve_gid(int gid, int nargs);

/* Most-derived method implementing vtable `slot` for class `ci`
 * (walks the base chain). -1 when the slot has no definition. */
int cpp_method_for_slot(int ci, int slot);

/* Class whose constructor is being compiled (for the vptr store), or
 * -1. Set by parse_method.c around gen_decl. */
void cpp_set_ctor_class(int ci);
int cpp_ctor_class(void);

/* ---- hooks.c helpers ------------------------------------------------- */

/*
 * Error recovery: consume the rest of the current declaration — a
 * depth-balanced walk that stops after a depth-0 ';' and leaves a
 * depth-0 '}' for the caller's loop. Always makes progress (>= 1 token)
 * so parse_translation_unit's no-progress guard never has to fire.
 */
void cpp_skip_decl(lexer_t *lx);

/* ---- parse_class.c (layout) ----------------------------------------- */

/* Append one data member to the class type (offset/alignment computed
 * here). `access` is stored in the per-field access table. */
void cpp_class_append_field(int ci, type_t *ftype, const char *name,
                            int access);

/* Attach `base_tag` as the (single) base: flattens its fields into the
 * derived type at offset 0. Reports cc_error for unknown/incomplete
 * bases, self-derivation and a second base (multiple inheritance is a
 * later phase). */
bool cpp_class_set_base(int ci, const char *base_tag);

/* Close the layout: round the size to the class alignment, force a
 * non-zero size and mark type + record complete. */
void cpp_class_finish(int ci);

/* ---- parse_method.c -------------------------------------------------- */

bool cpp_parse_ctor(lexer_t *lx, int ci, int access);
bool cpp_parse_dtor(lexer_t *lx, int ci, int access);
bool cpp_parse_method(lexer_t *lx, int ci, const char *name, type_t *ft,
                      int access, bool is_virtual, bool is_static);

/* ---- parse_ns.c ------------------------------------------------------ */

bool cpp_parse_namespace(lexer_t *lx);
bool cpp_parse_using(lexer_t *lx);

/* ---- parse_stmt.c (exceptions, Phase E) ------------------------------ */

bool cpp_parse_exception_stmt(lexer_t *lx);
int cpp_tag_for_type(type_t *t);
void cpp_eh_enter(void);
void cpp_eh_leave(lexer_t *lx);

/* ---- function templates (Phase F) ------------------------------------ */

#define CPP_MAX_TEMPLATES 32
#define CPP_MAX_TPARAMS 4

typedef struct {
    char name[CC_MAX_IDENT];
    int nparams;
    char params[CPP_MAX_TPARAMS][CC_MAX_IDENT];
    size_t start_off;
    size_t end_off;
} cpp_template_t;

extern cpp_template_t g_templates[CPP_MAX_TEMPLATES];
extern int g_n_templates;

/* ---- parse_tmpl.c (function templates, Phase F) ---------------------- */

bool cpp_parse_template(lexer_t *lx);
int cpp_template_find(const char *name);
bool cpp_template_instantiate(lexer_t *lx, const char *name, val_t *out);
void cpp_drain_pending_templates(void);

/* Access level recorded for an already declared (or newly defined)
 * out-of-line member; public when no declaration exists. */
int cpp_method_access(int ci, const char *name, int arity);

/* ---- class_table.c (access) ------------------------------------------ */

/* True when the current access context may read `mname` of class `st`;
 * reports cc_error otherwise (spec item 7). */
bool cpp_check_member_access(lexer_t *lx, const char *mname, type_t *st);

/* Class type for a bare C++ name (tags are not installed as global
 * typedefs; this keeps them from leaking into C files). Returns NULL
 * when `name` is not a class. */
type_t *cpp_type_by_name(const char *name);

/* Register a constructed class-typed local so its destructor runs at
 * scope exit / before return (Phase C; implemented in hooks.c). */
void cpp_register_dtor(int sym_idx, type_t *t);

#ifdef __cplusplus
}
#endif

#include "cpp/cpp_internal.h"

#endif /* CC_CPP_FRONT_H */
