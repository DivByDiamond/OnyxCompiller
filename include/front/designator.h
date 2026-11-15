/*
 * designator.h — C99 designated initializers (`.field`, `[index]`).
 */
#ifndef CC_DESIGNATOR_H
#define CC_DESIGNATOR_H

#include "core/types.h"
#include "front/lexer.h"

/* True when the current token starts a designator. */
bool init_at_designator(const lexer_t *lx);

/* Parse `.a[2].b =` against the aggregate `container`. On success sets the
 * index of the member chosen at the container level, plus the type and
 * byte offset (from the container start) of the innermost designated
 * object. Diagnoses unknown members and out-of-range indices. */
bool init_parse_designation(lexer_t *lx, type_t *container,
                            uint64_t *top_index, type_t **leaf_type,
                            uint64_t *leaf_offset);

/* One initializable subobject of an aggregate. */
typedef struct {
    type_t *type;
    uint64_t offset;   /* bytes from the start of the container */
} init_slot_t;

/* Advance through a brace list of `container`. `first` and `cursor` hold the
 * iteration state (start with true / 0). Consumes the separating comma and
 * any designator, then yields the next slot. Returns false at the closing
 * brace or when positional elements no longer fit; a comma that precedes
 * excess positional elements is left for the caller. */
bool init_next_slot(lexer_t *lx, type_t *container, bool *first,
                    uint64_t *cursor, init_slot_t *slot);

#endif
