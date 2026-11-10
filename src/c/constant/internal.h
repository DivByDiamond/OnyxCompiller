/* Internal typed integer constant evaluation interface. */
#ifndef CC_CONSTANT_INTERNAL_H
#define CC_CONSTANT_INTERNAL_H

#include "core/compat.h"
#include "core/types.h"
#include "front/parser_priv.h"

typedef struct { uint64_t bits; type_t *type; } const_value_t;

long long const_signed(uint64_t bits);
const_value_t const_value(uint64_t bits, type_t *type);
const_value_t const_expr_unary(bool evaluate);
const_value_t const_expr_conditional(bool evaluate);

#endif
