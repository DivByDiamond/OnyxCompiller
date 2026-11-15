/* A static local owns one private object per source declaration. */
#include "core/compat.h"
#include "core/cc.h"
#include "front/ast.h"
#include "front/parser_priv.h"
#include "back/gen.h"

void gen_static_local(lexer_t *lexer, type_t *type, const char *name) {
    /* Short numeric names avoid identifier truncation. The shared global
     * table makes the names unique across files and lexical scopes. */
    char backing[CC_MAX_IDENT];
    int serial = g_n_globals;
    do {
        snprintf(backing, sizeof(backing), "__sl_%d", serial++);
    } while (symtab_lookup_global(backing) >= 0);
    int global = symtab_install_global(backing, SYM_GLOBAL_VAR, type);
    /* Always install in the current scope. Looking up an outer alias here
     * would redirect the outer variable when a nested declaration shadows it. */
    int local = symtab_install_local(name, SYM_LOCAL_VAR, type, g_cur_scope);
    g_locals[local].alias_global = global;

    decl_t *decl = ast_new_decl(D_VAR, lexer->cur.pos);
    decl->type = type;
    strcpy(decl->name, backing);
    if (accept(T_ASSIGN)) decl->init = gen_parse_global_init(lexer, type);
    /* Keep the synthetic declaration unmangled; private linkage is applied
     * to the backing object after its load-time initializer is emitted. */
    gen_decl(decl);
    g_globals[global].is_static = true;
}

/* A static object's address is a load-time constant, even through its local
 * alias. An automatic local shadows globals but is not a constant address. */
int gen_initializer_symbol(const char *name) {
    if (g_func.in_function) {
        int local = symtab_lookup_local(name);
        if (local >= 0) return g_locals[local].alias_global;
    }
    return gen_lookup_global(name);
}
