/* Integer expression parser for #if, after defined and macro expansion. */
#include "core/compat.h"
#include "back/pp.h"

static const char *cursor;
static bool unsigned_value;
static long conditional(void);

static void whitespace(void) {
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n') cursor++;
}

static long primary(void) {
    long value = 0;
    unsigned_value = false;
    whitespace();
    if (*cursor == '(') {
        cursor++;
        value = conditional();
        whitespace();
        if (*cursor == ')') cursor++;
        else cc_error("missing ')' in preprocessor expression");
    } else if (*cursor == '\'') {
        cursor++;
        while (*cursor && *cursor != '\'') {
            int c = (unsigned char)*cursor++;
            if (c == '\\' && *cursor) {
                c = *cursor++;
                if (c == 'n') c = '\n';
                else if (c == 't') c = '\t';
                else if (c == 'r') c = '\r';
                else if (c == '0') c = 0;
            }
            value = (long)(((unsigned long)value << 8) | (unsigned char)c);
        }
        if (*cursor == '\'') cursor++;
        else cc_error("unterminated character in preprocessor expression");
    } else if (isdigit((unsigned char)*cursor)) {
        unsigned long number = 0;
        int base = 10;
        if (*cursor == '0') {
            base = 8;
            cursor++;
            if (*cursor == 'x' || *cursor == 'X') { base = 16; cursor++; }
        }
        while (isxdigit((unsigned char)*cursor)) {
            int digit = isdigit((unsigned char)*cursor) ? *cursor - '0'
                      : tolower((unsigned char)*cursor) - 'a' + 10;
            if (digit >= base) break;
            number = number * base + digit;
            cursor++;
        }
        if (base != 10 && number > 9223372036854775807UL) unsigned_value = true;
        while (*cursor == 'u' || *cursor == 'U' || *cursor == 'l' || *cursor == 'L') {
            if (*cursor == 'u' || *cursor == 'U') unsigned_value = true;
            cursor++;
        }
        value = (long)number;
    } else if (isalpha((unsigned char)*cursor) || *cursor == '_') {
        while (isalnum((unsigned char)*cursor) || *cursor == '_') cursor++;
    } else {
        cc_error("expected integer in preprocessor expression");
        if (*cursor) cursor++;
    }
    return value;
}

static long unary(void) {
    whitespace();
    if (*cursor == '!') { cursor++; long value = !unary(); unsigned_value = false; return value; }
    if (*cursor == '~') { cursor++; return ~unary(); }
    if (*cursor == '+') { cursor++; return unary(); }
    if (*cursor == '-') { cursor++; return (long)(0UL - (unsigned long)unary()); }
    return primary();
}

/* Operators are ordered from low to high precedence. Return the operator's
 * precedence without consuming it; length distinguishes && from & etc. */
static int operator_info(int *length) {
    whitespace();
    *length = 2;
    if (strncmp(cursor, "||", 2) == 0) return 1;
    if (strncmp(cursor, "&&", 2) == 0) return 2;
    if (strncmp(cursor, "==", 2) == 0 || strncmp(cursor, "!=", 2) == 0) return 6;
    if (strncmp(cursor, "<=", 2) == 0 || strncmp(cursor, ">=", 2) == 0) return 7;
    if (strncmp(cursor, "<<", 2) == 0 || strncmp(cursor, ">>", 2) == 0) return 8;
    *length = 1;
    if (*cursor == '|') return 3;
    if (*cursor == '^') return 4;
    if (*cursor == '&') return 5;
    if (*cursor == '<' || *cursor == '>') return 7;
    if (*cursor == '+' || *cursor == '-') return 9;
    if (*cursor == '*' || *cursor == '/' || *cursor == '%') return 10;
    return 0;
}

static long binary(int minimum) {
    long left = unary();
    bool left_unsigned = unsigned_value;
    for (;;) {
        int length;
        int precedence = operator_info(&length);
        if (precedence < minimum) { unsigned_value = left_unsigned; return left; }
        char op = cursor[0];
        char second = length == 2 ? cursor[1] : 0;
        cursor += length;
        long right = binary(precedence + 1);
        bool converted_unsigned = left_unsigned || unsigned_value;
        bool result_unsigned = converted_unsigned;
        if (precedence <= 2 || precedence == 6 || precedence == 7) result_unsigned = false;
        if (precedence == 8) result_unsigned = left_unsigned;
        unsigned long a = (unsigned long)left;
        unsigned long b = (unsigned long)right;
        if (op == '|' && second) left = left || right;
        else if (op == '&' && second) left = left && right;
        else if (op == '|') left |= right;
        else if (op == '^') left ^= right;
        else if (op == '&') left &= right;
        else if (op == '=') left = left == right;
        else if (op == '!') left = left != right;
        else if (op == '<' && second == '<') left = b < 64 ? (long)(a << b) : 0;
        else if (op == '>' && second == '>') left = b < 64 ? (left_unsigned ? (long)(a >> b) : left >> b) : 0;
        else if (op == '<') left = converted_unsigned ? (second ? a <= b : a < b) : (second ? left <= right : left < right);
        else if (op == '>') left = converted_unsigned ? (second ? a >= b : a > b) : (second ? left >= right : left > right);
        else if (op == '+') left = (long)(a + b);
        else if (op == '-') left = (long)(a - b);
        else if (op == '*') left = (long)(a * b);
        else if (op == '/') left = !right ? 0 : converted_unsigned ? (long)(a / b) : (a == (1UL << 63) && right == -1) ? left : left / right;
        else if (op == '%') left = !right ? 0 : converted_unsigned ? (long)(a % b) : (a == (1UL << 63) && right == -1) ? 0 : left % right;
        left_unsigned = result_unsigned;
    }
}

static long conditional(void) {
    long condition = binary(1);
    whitespace();
    if (*cursor != '?') return condition;
    cursor++;
    long yes = conditional();
    bool yes_unsigned = unsigned_value;
    whitespace();
    if (*cursor != ':') {
        cc_error("missing ':' in preprocessor expression");
        return 0;
    }
    cursor++;
    long no = conditional();
    unsigned_value = unsigned_value || yes_unsigned;
    return condition ? yes : no;
}

long pp_eval_integer(const char *expression) {
    cursor = expression;
    long value = conditional();
    whitespace();
    if (*cursor) cc_error("unexpected token in preprocessor expression: %s", cursor);
    return value;
}
