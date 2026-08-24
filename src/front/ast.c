/*
 * ast.c — AST nodes and symbol table.
 */
#include "core/compat.h"

#include "core/cc.h"
#include "front/ast.h"

cc_arena_t g_ast_arena;
cc_arena_t g_type_arena;

expr_t *ast_new_expr(expr_kind_t k, cc_pos_t pos) {
    expr_t *e = (expr_t *)cc_arena_alloc(&g_ast_arena, sizeof(expr_t), 8);
    e->kind = k;
    e->pos = pos;
    return e;
}

stmt_t *ast_new_stmt(stmt_kind_t k, cc_pos_t pos) {
    stmt_t *s = (stmt_t *)cc_arena_alloc(&g_ast_arena, sizeof(stmt_t), 8);
    s->kind = k;
    s->pos = pos;
    return s;
}

decl_t *ast_new_decl(decl_kind_t k, cc_pos_t pos) {
    decl_t *d = (decl_t *)cc_arena_alloc(&g_ast_arena, sizeof(decl_t), 8);
    d->kind = k;
    d->pos = pos;
    return d;
}

/* ---- Symbol table ---------------------------------------------------- */
sym_t g_globals[SYMTAB_GLOBAL_CAP];
int g_n_globals = 0;
sym_t g_locals[SYMTAB_LOCAL_CAP];
int g_n_locals = 0;
int g_cur_scope = 0;

/* Stack of starting indices for each scope. */
static int g_scope_stack[64];
static int g_scope_depth = 0;

void symtab_init(void) {
    g_n_globals = 0;
    g_n_locals = 0;
    g_cur_scope = 0;
    g_scope_depth = 0;
    g_scope_stack[0] = 0;
}

int symtab_lookup_global(const char *name) {
    for (int i = g_n_globals - 1; i >= 0; i--) {
        if (strcmp(g_globals[i].name, name) == 0) {
            g_globals[i].is_used = true;
            return i;
        }
    }
    return -1;
}

int symtab_lookup_local(const char *name) {
    for (int i = g_n_locals - 1; i >= 0; i--) {
        if (strcmp(g_locals[i].name, name) == 0) {
            g_locals[i].is_used = true;
            return i;
        }
    }
    return -1;
}

int symtab_install_global(const char *name, sym_kind_t kind, type_t *type) {
    if (g_n_globals >= SYMTAB_GLOBAL_CAP) cc_fatal("too many globals");
    /* Allow redeclaration only if types match (we skip check for MVP). */
    for (int i = 0; i < g_n_globals; i++) {
        if (strcmp(g_globals[i].name, name) == 0) {
            return i;  /* already declared; return existing slot */
        }
    }
    int idx = g_n_globals++;
    memset(&g_globals[idx], 0, sizeof(sym_t));
    strncpy(g_globals[idx].name, name, CC_MAX_IDENT - 1);
    g_globals[idx].kind = kind;
    g_globals[idx].type = type;
    g_globals[idx].scope = 0;
    return idx;
}

int symtab_install_local(const char *name, sym_kind_t kind, type_t *type, int scope) {
    if (g_n_locals >= SYMTAB_LOCAL_CAP) cc_fatal("too many locals");
    /* Search only current scope (>= scope). */
    for (int i = g_n_locals - 1; i >= 0; i--) {
        if (g_locals[i].scope < scope) break;
        if (strcmp(g_locals[i].name, name) == 0) {
            cc_error_at(NULL, 0, "redeclaration of '%s' in same scope", name);
            return i;
        }
    }
    int idx = g_n_locals++;
    memset(&g_locals[idx], 0, sizeof(sym_t));
    strncpy(g_locals[idx].name, name, CC_MAX_IDENT - 1);
    g_locals[idx].kind = kind;
    g_locals[idx].type = type;
    g_locals[idx].scope = scope;
    return idx;
}

void symtab_push_scope(void) {
    g_cur_scope++;
    if (g_scope_depth >= 63) cc_fatal("scope overflow");
    g_scope_stack[++g_scope_depth] = g_n_locals;
}

void symtab_pop_scope(void) {
    if (g_scope_depth <= 0) return;
    int new_n = g_scope_stack[g_scope_depth--];
    /* Don't actually erase, just truncate. */
    g_n_locals = new_n;
    g_cur_scope--;
}

void symtab_enter_function(type_t *ftype) {
    /* Reset locals for new function. */
    g_n_locals = 0;
    g_cur_scope = 0;
    g_scope_depth = 0;
    g_scope_stack[0] = 0;
}

void symtab_leave_function(void) {
    /* Nothing for now; could check unused here. */
}

void symtab_reset_locals(void) {
    g_n_locals = 0;
    g_cur_scope = 0;
    g_scope_depth = 0;
    g_scope_stack[0] = 0;
}
