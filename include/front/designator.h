/*
 * designator.h — C99 designated initializers (`.field`, `[index]`).
 */
#ifndef CC_DESIGNATOR_H
#define CC_DESIGNATOR_H

#include "core/types.h"
#include "front/lexer.h"

/* True when the current token starts a designator: `.f`, `[i]`, or the
 * obsolete GNU `f:`. */
bool init_at_designator(const lexer_t *lx);

/* One initializable subobject of an aggregate. */
typedef struct {
    type_t *type;
    uint64_t offset;   /* bytes from the start of the container */
    uint64_t repeat;   /* >1 for a GNU `[lo ... hi]` range */
    uint64_t stride;   /* bytes between repeated copies */
} init_slot_t;

/* Advance through a brace list of `container`. `first` and `cursor` hold the
 * iteration state (start with true / 0). Consumes the separating comma and
 * any designator, then yields the next slot. Returns false at the closing
 * brace or when positional elements no longer fit; a comma that precedes
 * excess positional elements is left for the caller. */
bool init_next_slot(lexer_t *lx, type_t *container, bool *first,
                    uint64_t *cursor, init_slot_t *slot);

/* Parse `.a[2].b =`, `[lo ... hi] =` or `a:` against `container`: fills
 * the slot and the index chosen at the container level. */
bool init_parse_designation(lexer_t *lx, type_t *container, init_slot_t *slot,
                            uint64_t *top_index);

#endif
