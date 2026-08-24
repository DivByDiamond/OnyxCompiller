/*
 * parser_priv.h — shared internals between parse.c and gen.c.
 *
 * Both files need access to the current lexer and basic accept/expect
 * helpers. Exposed here to avoid static-visibility conflicts.
 */
#ifndef CC_PARSER_PRIV_H
#define CC_PARSER_PRIV_H

#include "core/cc.h"
#include "lexer.h"

/* Global current lexer — set by parse_translation_unit(). */
extern lexer_t *g_lx;

/* Helpers. */
void parse_error(const char *msg);
void parse_expect(token_kind_t k, const char *what);
bool accept(token_kind_t k);

/* Skip __attribute__((...)) or asm(...) if present. */
void skip_attributes(void);

/* Set the current lexer (called by parse_translation_unit). */
void parser_set_lexer(lexer_t *lx);

#endif /* CC_PARSER_PRIV_H */
