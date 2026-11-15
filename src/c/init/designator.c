/* Designated initializer chains shared by local and global initializers. */
#include "core/compat.h"
#include "core/cc.h"
#include "front/designator.h"
#include "front/parser_priv.h"

bool init_at_designator(const lexer_t *lx) {
    return lx->cur.kind == T_DOT || lx->cur.kind == T_LBRACKET;
}

static bool designate_field(lexer_t *lx, type_t *type, uint64_t *index,
                            type_t **leaf, uint64_t *offset) {
    lex_next(lx); /* '.' */
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
        *index = (uint64_t)i;
        *offset += field->offset;
        *leaf = field->type;
        lex_next(lx);
        return true;
    }
    cc_error("no member named '%s' in initializer", lx->cur.text);
    return false;
}

static bool designate_element(lexer_t *lx, type_t *type, uint64_t *index,
                              type_t **leaf, uint64_t *offset) {
    lex_next(lx); /* '[' */
    if (type->kind != TY_ARRAY) {
        cc_error("array designator for non-array type");
        return false;
    }
    long long value = parse_const_expr();
    parse_expect(T_RBRACKET, "expected ']' in designator");
    bool bounded = type->length > 0 && !type->is_incomplete;
    if (value < 0 || (bounded && (uint64_t)value >= type->length)) {
        cc_error("array designator index %lld out of range", value);
        return false;
    }
    *index = (uint64_t)value;
    *offset += (uint64_t)value * type_sizeof(type->base);
    *leaf = type->base;
    return true;
}

bool init_parse_designation(lexer_t *lx, type_t *container,
                            uint64_t *top_index, type_t **leaf_type,
                            uint64_t *leaf_offset) {
    type_t *current = container;
    uint64_t offset = 0;
    uint64_t index = 0;
    bool first = true;
    while (init_at_designator(lx)) {
        uint64_t step = 0;
        bool ok = lx->cur.kind == T_DOT
                      ? designate_field(lx, current, &step, &current, &offset)
                      : designate_element(lx, current, &step, &current, &offset);
        if (!ok) return false;
        if (first) index = step;
        first = false;
    }
    parse_expect(T_ASSIGN, "expected '=' after designator");
    *top_index = index;
    *leaf_type = current;
    *leaf_offset = offset;
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
        if (!init_parse_designation(lx, container, cursor, &slot->type,
                                    &slot->offset))
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
    }
    *first = false;
    (*cursor)++;
    return true;
}
