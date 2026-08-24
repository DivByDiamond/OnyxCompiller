/*
 * ast.h — abstract syntax tree node types.
 *
 * OnyxCC uses a hybrid approach: AST nodes for declarations and
 * statements (because we need to know scope structure), but expressions
 * are compiled directly during parsing (single-pass codegen, like tcc).
 */
#ifndef CC_AST_H
#define CC_AST_H

#include "core/cc.h"
#include "core/types.h"
#include "lexer.h"

typedef enum {
    EX_NIL,            /* placeholder */
    EX_NUM,            /* integer literal */
    EX_STR,            /* string literal */
    EX_IDENT,          /* identifier reference */
    EX_BINOP,          /* binary op */
    EX_UNOP,           /* unary op */
    EX_ASSIGN,         /* assignment */
    EX_CALL,           /* function call */
    EX_INDEX,          /* array index */
    EX_MEMBER,         /* struct member (s.field) */
    EX_ARROW,          /* struct member (p->field) */
    EX_CAST,           /* (type)expr */
    EX_TERNARY,        /* cond ? a : b */
    EX_COMMA,          /* comma operator */
    EX_SIZEOF_TYPE,    /* sizeof(type) */
    EX_SIZEOF_EXPR,    /* sizeof expr */
    EX_ADDR,           /* &expr */
    EX_DEREF,          /* *expr */
    EX_PREINC, EX_PREDEC,
    EX_POSTINC, EX_POSTDEC,
    EX_INIT_LIST,      /* {1, 2, 3} */
    EX_LOGAND, EX_LOGOR,
    EX_COND,           /* placeholder for ternary */
} expr_kind_t;

typedef struct expr expr_t;

struct expr {
    expr_kind_t kind;
    type_t *type;     /* type of this expression (filled by semantic) */
    cc_pos_t pos;
    /* Generic children — reinterpreted based on kind. */
    expr_t *a, *b, *c;
    /* For literals. */
    uint64_t ival;
    char *str;
    size_t str_len;
    /* For identifiers: symbol table slot index. */
    int sym_idx;
    /* For binary/unary ops: token_kind. */
    int op;
    /* For calls: arg list (linked via a->next-style or array). */
    expr_t *args[8];
    int nargs;
    /* For member access: field index in struct. */
    int field_idx;
    /* For sizeof(type). */
    type_t *typeof_;
    /* For chained expressions (e.g. function arguments). */
    expr_t *next;
};

typedef enum {
    ST_EMPTY,
    ST_EXPR,
    ST_COMPOUND,
    ST_IF,
    ST_WHILE,
    ST_DO,
    ST_FOR,
    ST_RETURN,
    ST_BREAK,
    ST_CONTINUE,
    ST_DECL,         /* local variable declaration(s) */
    ST_GOTO,
    ST_LABEL,
    ST_SWITCH,
    ST_CASE,
    ST_DEFAULT,
} stmt_kind_t;

typedef struct stmt stmt_t;

struct stmt {
    stmt_kind_t kind;
    cc_pos_t pos;
    /* For ST_EXPR. */
    expr_t *expr;
    /* For ST_COMPOUND: list of statements. */
    stmt_t *body;        /* first child */
    /* For ST_IF: cond, then, else. */
    expr_t *cond;
    stmt_t *then_branch;
    stmt_t *else_branch;
    /* For ST_WHILE/ST_DO: cond, body. */
    /* For ST_FOR: init, cond, post, body. */
    stmt_t *for_init;
    expr_t *for_cond;
    expr_t *for_post;
    /* For ST_RETURN: expr. */
    /* For ST_DECL: linked list of (type, name, init) entries. */
    /* For ST_SWITCH: cond, body, list of cases. */
    /* For ST_LABEL/GOTO: label name. */
    char label[CC_MAX_IDENT];
    stmt_t *target;     /* for goto */
    /* Linked list inside compound. */
    stmt_t *next;
};

/* Top-level declaration. */
typedef enum {
    D_FUNC_DEF,     /* function definition (with body) */
    D_FUNC_DECL,    /* function prototype */
    D_VAR,          /* global variable */
    D_TYPEDEF,
    D_STRUCT,       /* struct/union/enum tag declaration */
    D_ENUM,
} decl_kind_t;

typedef struct decl decl_t;

struct decl {
    decl_kind_t kind;
    cc_pos_t pos;
    type_t *type;
    char name[CC_MAX_IDENT];
    /* For functions: body. */
    stmt_t *body;
    /* For variables: initializer (constant or zero). */
    expr_t *init;
    /* Storage class flags come from type (is_static etc.). */
    decl_t *next;
};

/* ---- Arena-based allocation helpers ---------------------------------- */
extern cc_arena_t g_ast_arena;
extern cc_arena_t g_type_arena;

expr_t *ast_new_expr(expr_kind_t k, cc_pos_t pos);
stmt_t *ast_new_stmt(stmt_kind_t k, cc_pos_t pos);
decl_t *ast_new_decl(decl_kind_t k, cc_pos_t pos);

/* Symbol table. */
typedef enum {
    SYM_NONE,
    SYM_TYPEDEF,
    SYM_ENUM_CONST,
    SYM_LOCAL_VAR,
    SYM_GLOBAL_VAR,
    SYM_FUNCTION,
    SYM_BUILTIN,
} sym_kind_t;

typedef struct {
    sym_kind_t kind;
    char name[CC_MAX_IDENT];
    type_t *type;
    /* For locals: stack offset (positive = above fp). */
    int64_t offset;
    /* For globals: virtual address (filled during codegen). */
    uint64_t vaddr;
    /* For functions: text offset. */
    uint32_t text_off;
    /* For enum constants: value. */
    int64_t enum_val;
    /* Scope level. */
    int scope;
    bool is_used;
    bool is_defined;
    /* For builtins: index. */
    int builtin_id;
} sym_t;

#define SYMTAB_GLOBAL_CAP CC_MAX_GLOBALS
#define SYMTAB_LOCAL_CAP  CC_MAX_LOCALS

void symtab_init(void);

/* Returns index ≥ 0 on success, -1 if not found. */
int symtab_lookup_global(const char *name);
int symtab_lookup_local(const char *name);

/* Installs a new symbol. Returns its index. Errors if duplicate in
 * current scope. */
int symtab_install_global(const char *name, sym_kind_t kind, type_t *type);
int symtab_install_local(const char *name, sym_kind_t kind, type_t *type, int scope);

/* Push/pop scope for locals. */
void symtab_push_scope(void);
void symtab_pop_scope(void);

/* Reset local symbol table (keeps globals intact). */
void symtab_reset_locals(void);

void symtab_enter_function(type_t *ftype);
void symtab_leave_function(void);

extern sym_t g_globals[SYMTAB_GLOBAL_CAP];
extern int g_n_globals;
extern sym_t g_locals[SYMTAB_LOCAL_CAP];
extern int g_n_locals;
extern int g_cur_scope;

#endif /* CC_AST_H */
