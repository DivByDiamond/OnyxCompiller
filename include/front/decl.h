/*
 * decl.h — full C declarator parsing (pointers, arrays, function types,
 *          function pointers).
 *
 * Implements the classic declarator grammar:
 *
 *   declarator      := pointer? direct-declarator
 *   direct-declarator := identifier
 *                     | '(' declarator ')'
                     | direct-declarator '(' param-list ')'
 *                     | direct-declarator '[' const-expr? ']'
 *
 * This handles patterns the old ad-hoc parser could not, most notably
 * function pointers:
 *
 *   int (*fp)(int, char);              — variable
 *   int (*table[4])(void);             — array of function pointers
 *   void sort(void *base, int (*cmp)(const void *, const void *)); — params
 *   typedef void (*handler_t)(int);    — typedef
 *   struct dev { int (*read)(char *, int); };  — struct field
 *   (void (*)(int))ptr                 — cast / abstract declarator
 */
#ifndef CC_DECL_H
#define CC_DECL_H

#include "core/cc.h"
#include "core/types.h"
#include "front/lexer.h"

/*
 * Parse a complete declarator. `base` is the base type produced by
 * declaration specifiers (parse_decl_spec / parse_type_spec).
 *
 * On return:
 *   - *out_name holds the declared identifier, or "" for an abstract
 *     declarator (e.g. in casts and prototypes: `int (*)(void)`).
 *   - Returns the type of the DECLARED SYMBOL. For `int (*fp)(void)` this
 *     is ptr-to-function-returning-int, NOT a function type.
 *
 * The lexer is left at the first token after the declarator.
 */
type_t *parse_declarator(lexer_t *lx, type_t *base,
                         char *out_name, size_t name_sz);

/*
 * Parse only the suffix chain of a direct declarator:
 *   '(' param-list ')'  → function type returning `base`
 *   '[' const-expr ']'  → array of `base`
 * Multiple suffixes nest right-to-left (`int f[3][4]`, `(*g[2])(int)`).
 * Returns `base` unchanged if no suffix is present.
 * `base` may be NULL while parsing the inner part of a grouped declarator;
 * the caller splices the real base in later with type_splice_base().
 */
type_t *parse_type_suffix(lexer_t *lx, type_t *base);

/*
 * Replace the NULL bottom of a type chain built by parse_declarator's
 * inner pass with `base`. Returns the possibly-new chain head.
 */
type_t *type_splice_base(type_t *t, type_t *base);

/*
 * Parse a parameter list after '(' into a freshly allocated TY_FUNC.
 * Handles function-pointer parameters, abstract declarators, arrays
 * (decayed to pointers) and `...` varargs. Expects '(' already consumed.
 * Leaves lexer at ')'.
 */
void parse_param_list(lexer_t *lx, type_t *ftype);

#endif /* CC_DECL_H */
