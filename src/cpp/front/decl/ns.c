/*
 * parse_ns.c — namespace support (Phase D, flattened).
 *
 * OnyxCC has no separate namespace scope object: every declaration is
 * registered in the flat symbol table. A namespace body is therefore
 * parsed as an ordinary top-level sequence and its members become global
 * names; `using namespace N;` is accepted as a no-op and qualified
 * references (`N::name`, `A::B::name`) resolve to the final component.
 *
 * This keeps C and C++ symbol spaces interoperable and avoids a second
 * lookup path everywhere. The documented limitation (docs/STATUS.md) is
 * that two namespaces cannot both declare the same member name.
 */
#include "core/compat.h"

#include "core/cc.h"
#include "core/types.h"
#include "front/lexer.h"
#include "front/parser_priv.h"
#include "cpp/cpp_front.h"

bool cpp_parse_namespace(lexer_t *lx) {
    lex_next(lx);                       /* namespace */
    while (lx->cur.kind == T_IDENT) {
        lex_next(lx);
        if (accept(T_SCOPE)) continue;  /* namespace A::B -> flatten both */
        break;
    }
    if (lx->cur.kind != T_LBRACE) {
        parse_error("expected '{' after namespace name");
        cpp_skip_decl(lx);
        return true;
    }
    lex_next(lx);
    while (lx->cur.kind != T_RBRACE && lx->cur.kind != T_EOF)
        parse_top_level_decl();
    parse_expect(T_RBRACE, "expected '}' after namespace body");
    return true;
}

bool cpp_parse_using(lexer_t *lx) {
    /* `using namespace N;` and `using N::x;` — flattened namespaces make
     * both directives no-ops; consume through the terminating ';'. */
    while (lx->cur.kind != T_SEMI && lx->cur.kind != T_EOF)
        lex_next(lx);
    if (lx->cur.kind == T_SEMI) lex_next(lx);
    return true;
}
