/* Target-width integer arithmetic, precedence and conditional evaluation. */
#include "internal.h"

static int const_op_prec(token_kind_t k) {
    switch (k) {
        case T_OR: return 1;
        case T_AND: return 2;
        case T_PIPE: return 3;
        case T_CARET: return 4;
        case T_AMP: return 5;
        case T_EQ: case T_NE: return 6;
        case T_LT: case T_GT: case T_LE: case T_GE: return 7;
        case T_SHL: case T_SHR: return 8;
        case T_PLUS: case T_MINUS: return 9;
        case T_STAR: case T_SLASH: case T_PERCENT: return 10;
        default: return 0;
    }
}

long long const_signed(uint64_t bits) {
    return bits <= 0x7fffffffffffffffULL ? (long long)bits : -1 - (long long)~bits;
}

/* Keep sign extension explicit; all arithmetic below uses unsigned bits so
 * malformed/overflowing target expressions cannot invoke host C UB. */
const_value_t const_value(uint64_t bits, type_t *type) {
    unsigned width = (unsigned)type_sizeof(type) * 8;
    if (type->kind == TY_BOOL) bits = bits != 0;
    else if (width && width < 64) {
        uint64_t mask = (1ULL << width) - 1;
        bits &= mask;
        if (type_is_signed(type) && (bits & (1ULL << (width - 1)))) bits |= ~mask;
    }
    const_value_t value = { bits, type };
    return value;
}


static const_value_t const_expr_binop(int min_prec, bool evaluate) {
    const_value_t lhs = const_expr_unary(evaluate);
    for (;;) {
        int prec = const_op_prec(g_lx->cur.kind);
        if (prec < min_prec || prec == 0) break;
        token_kind_t op = g_lx->cur.kind;
        lex_next(g_lx);
        bool rhs_evaluate = evaluate;
        if ((op == T_AND && !lhs.bits) || (op == T_OR && lhs.bits)) rhs_evaluate = false;
        const_value_t rhs = const_expr_binop(prec + 1, rhs_evaluate);
        type_t *common = type_common(lhs.type, rhs.type);
        if (op == T_SHL || op == T_SHR) common = type_promote(lhs.type);
        lhs = const_value(lhs.bits, common);
        rhs = const_value(rhs.bits, (op == T_SHL || op == T_SHR) ? type_promote(rhs.type) : common);
        uint64_t a = lhs.bits, b = rhs.bits, result = 0;
        bool sign = type_is_signed(common), comparison = false;
        switch (op) {
            case T_PLUS: result = a + b; break;
            case T_MINUS: result = a - b; break;
            case T_STAR: result = a * b; break;
            case T_SLASH: case T_PERCENT: {
                if (!b) {
                    if (evaluate) parse_error("division by zero in integer constant");
                    break;
                }
                bool negative_a = sign && const_signed(a) < 0;
                bool negative_b = sign && const_signed(b) < 0;
                uint64_t magnitude_a = negative_a ? 0ULL - a : a;
                uint64_t magnitude_b = negative_b ? 0ULL - b : b;
                result = op == T_SLASH ? magnitude_a / magnitude_b : magnitude_a % magnitude_b;
                if (op == T_SLASH ? negative_a != negative_b : negative_a) result = 0ULL - result;
                break;
            }
            case T_SHL: case T_SHR:
                if (b >= type_sizeof(common) * 8) {
                    if (evaluate) parse_error("invalid shift count in integer constant");
                    break;
                }
                result = op == T_SHL ? a << b : a >> b;
                if (op == T_SHR && b && sign && const_signed(a) < 0)
                    result |= ~0ULL << (64 - b);
                break;
            case T_AMP: result = a & b; break;
            case T_PIPE: result = a | b; break;
            case T_CARET: result = a ^ b; break;
            case T_AND: result = a && b; comparison = true; break;
            case T_OR: result = a || b; comparison = true; break;
            case T_EQ: result = a == b; comparison = true; break;
            case T_NE: result = a != b; comparison = true; break;
            case T_LT: result = sign ? const_signed(a) < const_signed(b) : a < b; comparison = true; break;
            case T_GT: result = sign ? const_signed(a) > const_signed(b) : a > b; comparison = true; break;
            case T_LE: result = sign ? const_signed(a) <= const_signed(b) : a <= b; comparison = true; break;
            case T_GE: result = sign ? const_signed(a) >= const_signed(b) : a >= b; comparison = true; break;
            default: break;
        }
        lhs = const_value(result, comparison ? &ty_int : common);
    }
    return lhs;
}

const_value_t const_expr_conditional(bool evaluate) {
    const_value_t condition = const_expr_binop(0, evaluate);
    if (!accept(T_QUESTION)) return condition;
    const_value_t yes = const_expr_conditional(evaluate && condition.bits != 0);
    parse_expect(T_COLON, "expected ':' in constant conditional");
    const_value_t no = const_expr_conditional(evaluate && condition.bits == 0);
    return const_value(condition.bits ? yes.bits : no.bits, type_common(yes.type, no.type));
}

long long parse_const_expr(void) {
    return const_signed(const_expr_conditional(true).bits);
}
