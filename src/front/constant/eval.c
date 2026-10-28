/* Constant expression operands: sizeof, offsetof, casts and literals. */
#include "internal.h"
#include "front/ast.h"
#include "front/parse.h"
#include "front/decl.h"
#include "back/gen.h"

/* Consume only the builtin, so runtime callers retain surrounding operators. */
long long parse_builtin_offsetof(void) {
    lex_next(g_lx);
    parse_expect(T_LPAREN, "expected '(' after __builtin_offsetof");
    type_t *type = parse_type_spec(g_lx);
    char name[CC_MAX_IDENT];
    type = parse_declarator(g_lx, type, name, sizeof(name));
    parse_expect(T_COMMA, "expected ',' before member designator");
    uint64_t offset = 0;
    for (;;) {
        if (g_lx->cur.kind != T_IDENT || !type ||
            (type->kind != TY_STRUCT && type->kind != TY_UNION)) {
            parse_error("expected aggregate member in offsetof");
            return 0;
        }
        int field = 0;
        while (field < type->nfields &&
               strcmp(type->fields[field].name, g_lx->cur.text)) field++;
        if (field == type->nfields) {
            parse_error("unknown member in offsetof");
            return 0;
        }
        if (type->fields[field].bit_width) {
            parse_error("offsetof cannot address a bit-field");
            return 0;
        }
        offset += type->fields[field].offset;
        type = type->fields[field].type;
        lex_next(g_lx);
        while (accept(T_LBRACKET)) {
            if (type->kind != TY_ARRAY) {
                parse_error("offsetof subscript requires array member");
                return 0;
            }
            offset += (uint64_t)parse_const_expr() * type_sizeof(type->base);
            type = type->base;
            parse_expect(T_RBRACKET, "expected ']' in offsetof");
        }
        if (!accept(T_DOT)) break;
    }
    parse_expect(T_RPAREN, "expected ')' after offsetof");
    return const_signed(offset);
}

const_value_t const_expr_unary(bool evaluate) {
    if (g_lx->cur.kind == T_IDENT && !strcmp(g_lx->cur.text, "__builtin_offsetof"))
        return const_value((uint64_t)parse_builtin_offsetof(), &ty_ulong);
    if (accept(T_KW_SIZEOF)) {
        bool grouped = accept(T_LPAREN);
        type_t *type = NULL;
        if (is_type_start(g_lx)) {
            type = parse_type_spec(g_lx);
            char name[CC_MAX_IDENT];
            type = parse_declarator(g_lx, type, name, sizeof(name));
        } else if (g_lx->cur.kind == T_STRING) {
            long long length = g_lx->cur.str_len + 1;
            lex_next(g_lx);
            if (grouped) parse_expect(T_RPAREN, "expected ')' after sizeof");
            return const_value((uint64_t)length, &ty_ulong);
        } else if (g_lx->cur.kind == T_IDENT) {
            int index = symtab_lookup_local(g_lx->cur.text);
            if (index >= 0) type = g_locals[index].type;
            else {
                index = gen_lookup_global(g_lx->cur.text);
                if (index < 0 && g_opts.n_input_files > 1) {
                    char mangled[CC_MAX_IDENT + 16];
                    mangle_static_name(g_lx->cur.text, mangled, sizeof(mangled));
                    index = gen_lookup_global(mangled);
                }
                if (index >= 0) type = g_globals[index].type;
            }
            lex_next(g_lx);
        } else {
            parse_error("expected type or object after sizeof");
        }
        if (grouped) parse_expect(T_RPAREN, "expected ')' after sizeof");
        if (!type) parse_error("unknown object in sizeof");
        return const_value(type ? type_sizeof(type) : 0, &ty_ulong);
    }
    if (g_lx->cur.kind == T_PLUS || g_lx->cur.kind == T_MINUS ||
        g_lx->cur.kind == T_NOT || g_lx->cur.kind == T_TILDE) {
        token_kind_t op = g_lx->cur.kind;
        lex_next(g_lx);
        const_value_t value = const_expr_unary(evaluate);
        if (op == T_NOT) return const_value(!value.bits, &ty_int);
        value = const_value(value.bits, type_promote(value.type));
        if (op == T_MINUS) value.bits = 0ULL - value.bits;
        if (op == T_TILDE) value.bits = ~value.bits;
        return const_value(value.bits, value.type);
    }
    if (accept(T_LPAREN)) {
        if (is_type_start(g_lx)) {
            type_t *cast = parse_type_spec(g_lx);
            char name[CC_MAX_IDENT];
            cast = parse_declarator(g_lx, cast, name, sizeof(name));
            parse_expect(T_RPAREN, "expected ')' after constant cast");
            const_value_t value = const_expr_unary(evaluate);
            if (!type_is_integer(cast) && cast->kind != TY_PTR) {
                parse_error("constant cast requires integer or pointer type");
                return const_value(0, &ty_int);
            }
            return const_value(value.bits, cast);
        }
        const_value_t v = const_expr_conditional(evaluate);
        parse_expect(T_RPAREN, "expected ')'");
        return v;
    }
    if (g_lx->cur.kind == T_CHAR_LIT || g_lx->cur.kind == T_INT || g_lx->cur.kind == T_LONG ||
        g_lx->cur.kind == T_UINT || g_lx->cur.kind == T_ULONG) {
        type_t *type = &ty_int;
        if (g_lx->cur.kind == T_UINT) type = &ty_uint;
        if (g_lx->cur.kind == T_LONG) type = &ty_long;
        if (g_lx->cur.kind == T_ULONG) type = &ty_ulong;
        const_value_t value = const_value(g_lx->cur.ival, type);
        lex_next(g_lx);
        return value;
    }
    if (g_lx->cur.kind == T_IDENT) {
        /* Try enum constant lookup. */
        int idx = gen_lookup_global(g_lx->cur.text);
        if (idx >= 0 && g_globals[idx].kind == SYM_ENUM_CONST) {
            const_value_t value = const_value((uint64_t)g_globals[idx].enum_val, &ty_int);
            lex_next(g_lx);
            return value;
        }
        parse_error("identifier is not an integer constant");
        lex_next(g_lx);
        return const_value(0, &ty_int);
    }
    parse_error("expected constant expression");
    return const_value(0, &ty_int);
}
