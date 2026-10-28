/*
 * parse.h — top-level parser interface.
 */
#ifndef CC_PARSE_H
#define CC_PARSE_H

#include "core/cc.h"
#include "lexer.h"

void parse_translation_unit(lexer_t *lx);
long long parse_builtin_offsetof(void);

#endif /* CC_PARSE_H */
