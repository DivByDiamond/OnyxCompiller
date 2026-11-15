/* Designated initializer chains shared by local and global initializers. */
#include "core/compat.h"
#include "core/cc.h"
#include "front/designator.h"
#include "front/parser_priv.h"

bool init_at_designator(const lexer_t *lx) {
    if (lx->cur.kind == T_DOT || lx->cur.kind == T_LBRACKET) return true;
    if (lx->cur.kind != T_IDENT) return false;
    lexer_t saved = *lx;
    lex_next(&saved);
    return saved.cur.kind == T_COLON;
}

/* Where a range was seen: at most one `[lo ... hi]` per designation. */
typedef struct {
    uint64_t index;    /* member chosen at the container level */
    uint64_t offset;   /* bytes to the innermost designated object */
    uint64_t repeat;
    uint64_t stride;
    bool first_is_range;
} designation_t;

static bool designate_field(lexer_t *lx, type_t *type, designation_t *d,
                            uint64_t *step, type_t **leaf) {
    if (lx->cur.kind == T_DOT) lex_next(lx);
    if (lx->cur.kind != T_IDENT) {
        cc_error("expected member name in designator");
        return false;
    }
    if (type->kind != TY_STRUCT && type->kind != TY_UNION) {
        cc_error("member designator for non-struct type");
        return false;
    }
    for (int i = 0; i < type->nfields; i++) {
        struct_field_t *field = &type->fields[i];
        if (field->is_anon || strcmp(field->name, lx->cur.text) != 0) continue;
        *step = (uint64_t)i;
        d->offset += field->offset;
        *leaf = field->type;
        lex_next(lx);
        return true;
    }
    cc_error("no member named '%s' in initializer", lx->cur.text);
    return false;
}

static bool designate_element(lexer_t *lx, type_t *type, designation_t *d,
                              uint64_t *step, type_t **leaf, bool first) {
    lex_next(lx); /* '[' */
    if (type->kind != TY_ARRAY) {
        cc_error("array designator for non-array type");
        return false;
    }
    long long low = parse_const_expr();
    long long high = low;
    bool range = accept(T_ELLIPSIS);
    if (range) high = parse_const_expr();
    parse_expect(T_RBRACKET, "expected ']' in designator");
    bool bounded = type->length > 0 && !type->is_incomplete;
    if (low < 0 || high < low || (bounded && (uint64_t)high >= type->length)) {
        cc_error("array designator index %lld out of range", high);
        return false;
    }
    if (range && d->repeat > 1) {
        cc_error("only one range designator is supported per initializer");
        return false;
    }
    uint64_t elem_size = type_sizeof(type->base);
    if (range) {
        d->repeat = (uint64_t)(high - low) + 1;
        d->stride = elem_size;
        d->first_is_range = first;
    }
    *step = range && first ? (uint64_t)high : (uint64_t)low;
    d->offset += (uint64_t)low * elem_size;
    *leaf = type->base;
    return true;
}

bool init_parse_designation(lexer_t *lx, type_t *container, init_slot_t *slot,
                            uint64_t *top_index) {
    type_t *current = container;
    designation_t d = { 0, 0, 1, 0, false };
    bool first = true;
    bool obsolete = lx->cur.kind == T_IDENT;
    while (init_at_designator(lx)) {
        uint64_t step = 0;
        bool ok = lx->cur.kind == T_LBRACKET
                      ? designate_element(lx, current, &d, &step, &current, first)
                      : designate_field(lx, current, &d, &step, &current);
        if (!ok) return false;
        if (first) d.index = step;
        first = false;
        if (obsolete) break;
    }
    parse_expect(obsolete ? T_COLON : T_ASSIGN, "expected '=' after designator");
    *top_index = d.index;
    slot->type = current;
    slot->offset = d.offset;
    slot->repeat = d.repeat;
    slot->stride = d.stride;
    return true;
}

/* Skip unnamed members; true when positional member `*cursor` exists. */
static bool positional_room(type_t *container, uint64_t *cursor) {
    if (container->kind == TY_ARRAY)
        return container->length == 0 || container->is_incomplete ||
               *cursor < container->length;
    while (*cursor < (uint64_t)container->nfields &&
           container->fields[*cursor].is_anon)
        (*cursor)++;
    return *cursor < (uint64_t)container->nfields;
}

static bool comma_precedes_designator(lexer_t *lx) {
    lexer_t saved = *lx;
    lex_next(lx);
    bool designator = init_at_designator(lx);
    *lx = saved;
    return designator;
}

bool init_next_slot(lexer_t *lx, type_t *container, bool *first,
                    uint64_t *cursor, init_slot_t *slot) {
    if (lx->cur.kind == T_RBRACE || lx->cur.kind == T_EOF) return false;
    if (!*first) {
        if (lx->cur.kind != T_COMMA) return false;
        if (!comma_precedes_designator(lx) && !positional_room(container, cursor))
            return false;
        lex_next(lx);
        if (lx->cur.kind == T_RBRACE) return false;
    }
    if (init_at_designator(lx)) {
        if (!init_parse_designation(lx, container, slot, cursor))
            return false;
    } else {
        if (!positional_room(container, cursor)) return false;
        if (container->kind == TY_ARRAY) {
            slot->type = container->base;
            slot->offset = *cursor * type_sizeof(container->base);
        } else {
            slot->type = container->fields[*cursor].type;
            slot->offset = container->fields[*cursor].offset;
        }
        slot->repeat = 1;
        slot->stride = 0;
    }
    *first = false;
    (*cursor)++;
    return true;
}
