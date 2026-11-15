/* Declaration attributes. Weak is meaningful only when explicitly read by
 * the top-level parser; skip_attributes must never erase that information. */
#include "core/compat.h"
#include "front/parser_priv.h"

static bool named(const char *name) {
    return g_lx->cur.kind == T_IDENT && strcmp(g_lx->cur.text, name) == 0;
}

static void skip_group(void) {
    parse_expect(T_LPAREN, "expected '(' after attribute");
    int depth = 1;
    while (depth && g_lx->cur.kind != T_EOF) {
        if (g_lx->cur.kind == T_LPAREN) depth++;
        if (g_lx->cur.kind == T_RPAREN) depth--;
        lex_next(g_lx);
    }
    if (depth) parse_error("unterminated attribute arguments");
}

bool read_attributes(void) {
    bool weak = false;
    for (;;) {
        if (named("__weak__")) {
            weak = true;
            lex_next(g_lx);
            if (g_lx->cur.kind == T_LPAREN)
                parse_error("weak attribute takes no arguments");
            continue;
        }
        if (named("asm") || named("__asm__")) {
            lex_next(g_lx);
            while (g_lx->cur.kind == T_LPAREN) skip_group();
            continue;
        }
        if (!named("__attribute__") && !named("__attribute")) break;
        lex_next(g_lx);
        parse_expect(T_LPAREN, "expected '((' after __attribute__");
        parse_expect(T_LPAREN, "expected '((' after __attribute__");
        while (g_lx->cur.kind != T_RPAREN && g_lx->cur.kind != T_EOF) {
            if (accept(T_COMMA)) continue;
            bool this_weak = named("weak") || named("__weak__");
            if (this_weak) weak = true;
            lex_next(g_lx);
            if (this_weak && g_lx->cur.kind == T_LPAREN)
                parse_error("weak attribute takes no arguments");
            while (g_lx->cur.kind == T_LPAREN) skip_group();
            if (g_lx->cur.kind != T_COMMA && g_lx->cur.kind != T_RPAREN)
                parse_error("expected ',' or ')' in attribute list");
        }
        parse_expect(T_RPAREN, "expected ')' after attribute list");
        parse_expect(T_RPAREN, "expected ')' after __attribute__");
    }
    return weak;
}

void skip_attributes(void) {
    if (read_attributes())
        parse_error("weak attribute is only supported on top-level declarations");
}
