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

/* Per-input-file C++ mode (set by main.c from the file extension before
 * preprocess/lex/parse; defined in parse.c). All cpp_* hooks and gen.c
 * seams are gated on this — C files keep it false. */
extern bool g_cpp_mode;

/* Helpers. */
void parse_error(const char *msg);
void parse_expect(token_kind_t k, const char *what);
bool accept(token_kind_t k);

/* Constant expression evaluator (parse.c) — uses g_lx. */
long long parse_const_expr(void);

/* Skip __attribute__((...)) or asm(...) if present. */
void skip_attributes(void);

/* Set the current lexer (called by parse_translation_unit). */
void parser_set_lexer(lexer_t *lx);

/* Parse one top-level declaration through the normal C/C++ dispatch
 * (used by the namespace body loop). */
void parse_top_level_decl(void);

#endif /* CC_PARSER_PRIV_H */
