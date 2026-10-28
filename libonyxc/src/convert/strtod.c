/* Binary64 parser with integer-rational rounding, nearest/ties-even.
 * Retain 800 significant digits plus sticky information: binary64 rounding
 * midpoints have fewer than 800 significant decimal (or hexadecimal) digits.
 * This bounds work/storage while still consuming arbitrarily long input. */
#include "onyxc.h"

#define WORDS 128
struct integer { uint32_t word[WORDS]; int used; };
static void trim(struct integer *n) { while (n->used > 1 && !n->word[n->used - 1]) n->used--; }
static void small(struct integer *n, uint32_t value) {
    memset(n, 0, sizeof(*n)); n->word[0] = value; n->used = 1;
}
static void multiply(struct integer *n, unsigned factor, unsigned add) {
    uint64_t carry = add;
    for (int i = 0; i < n->used; ++i) {
        carry += (uint64_t)n->word[i] * factor;
        n->word[i] = (uint32_t)carry; carry >>= 32;
    }
    if (carry) n->word[n->used++] = (uint32_t)carry;
}
static void shift(struct integer *n, int bits) {
    int words = bits / 32, extra = bits % 32;
    uint64_t carry = 0;
    if (words) {
        for (int i = n->used - 1; i >= 0; --i) n->word[i + words] = n->word[i];
        for (int i = 0; i < words; ++i) n->word[i] = 0;
        n->used += words;
    }
    for (int i = words; i < n->used; ++i) {
        uint64_t value = ((uint64_t)n->word[i] << extra) | carry;
        n->word[i] = (uint32_t)value; carry = value >> 32;
    }
    if (carry) n->word[n->used++] = (uint32_t)carry;
}
static int compare(struct integer *a, struct integer *b) {
    if (a->used != b->used) return a->used > b->used ? 1 : -1;
    for (int i = a->used - 1; i >= 0; --i)
        if (a->word[i] != b->word[i]) return a->word[i] > b->word[i] ? 1 : -1;
    return 0;
}
static void subtract(struct integer *a, struct integer *b) {
    uint64_t borrow = 0;
    for (int i = 0; i < a->used; ++i) {
        uint64_t value = (i < b->used ? b->word[i] : 0) + borrow;
        borrow = (uint64_t)a->word[i] < value;
        a->word[i] = (uint32_t)((uint64_t)a->word[i] - value);
    }
    trim(a);
}
static int bit_length(struct integer *n) {
    uint32_t top = n->word[n->used - 1];
    int bits = (n->used - 1) * 32;
    while (top) { bits++; top >>= 1; }
    return bits;
}
static double from_bits(uint64_t bits) { double value; memcpy(&value, &bits, 8); return value; }
static double range_result(int overflow, int negative) {
    errno = ERANGE;
    return from_bits((negative ? 0x8000000000000000UL : 0) |
                     (overflow ? 0x7ff0000000000000UL : 0));
}
static double rational(struct integer *n, struct integer *den, long power, int sticky, int negative) {
    struct integer temp;
    int exponent = bit_length(n) - bit_length(den), shift_by;
    long binary;
    uint64_t q = 0, sign = negative ? 0x8000000000000000UL : 0;
    if (exponent >= 0) { temp = *den; shift(&temp, exponent); if (compare(n, &temp) < 0) exponent--; }
    else { temp = *n; shift(&temp, -exponent); if (compare(&temp, den) < 0) exponent--; }
    binary = exponent + power;
    if (binary > 1023) return range_result(1, negative);
    if (binary < -1075) return range_result(0, negative);
    shift_by = binary >= -1022 ? 52 - exponent : (int)(power + 1074);
    if (shift_by >= 0) shift(n, shift_by); else shift(den, -shift_by);
    for (int i = bit_length(n) - bit_length(den); i >= 0; --i) {
        temp = *den; shift(&temp, i);
        if (compare(n, &temp) >= 0) { subtract(n, &temp); q |= 1UL << i; }
    }
    int inexact = bit_length(n) != 0 || sticky;
    shift(n, 1);
    int halfway = compare(n, den);
    if (halfway > 0 || (!halfway && (sticky || (q & 1)))) q++;
    if (binary < -1022) {
        if (inexact) errno = ERANGE;
        return from_bits(sign | q);
    }
    if (q == 0x20000000000000UL) { q >>= 1; binary++; }
    if (binary > 1023) return range_result(1, negative);
    return from_bits(sign | ((uint64_t)(binary + 1023) << 52) | (q & 0xfffffffffffffUL));
}
static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }
static int matches(const char *s, const char *word) {
    while (*word) if (lower((unsigned char)*s++) != *word++) return 0;
    return 1;
}
static int digit(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = lower(c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}
static long exponent_part(const char **end, int marker) {
    const char *s = *end, *start;
    long result = 0;
    int negative = 0;
    if (lower((unsigned char)*s) != marker) return 0;
    s++;
    if (*s == '+' || *s == '-') { negative = *s == '-'; s++; }
    start = s;
    while (*s >= '0' && *s <= '9') {
        if (result < LONG_MAX / 160) result = result * 10 + (*s - '0');
        s++;
    }
    if (s == start) return 0;
    *end = s;
    return negative ? -result : result;
}

double strtod(const char *input, char **endp) {
    const char *s = input;
    struct integer numerator, denominator;
    long fraction = 0, dropped = 0, power;
    int negative = 0, base = 10, point = 0, any = 0, kept = 0, sticky = 0;
    if (endp) *endp = (char *)input;
    while (*s == ' ' || (*s >= '\t' && *s <= '\r')) s++;
    if (*s == '+' || *s == '-') { negative = *s == '-'; s++; }
    if (matches(s, "inf")) {
        s += 3; if (matches(s, "inity")) s += 5;
        if (endp) *endp = (char *)s;
        return from_bits((negative ? 0x8000000000000000UL : 0) | 0x7ff0000000000000UL);
    }
    if (matches(s, "nan")) {
        s += 3;
        if (*s == '(') {
            const char *p = s + 1;
            while ((*p >= '0' && *p <= '9') || (lower((unsigned char)*p) >= 'a' &&
                   lower((unsigned char)*p) <= 'z') || *p == '_') p++;
            if (*p == ')') s = p + 1;
        }
        if (endp) *endp = (char *)s;
        return from_bits((negative ? 0x8000000000000000UL : 0) | 0x7ff8000000000000UL);
    }
    if (s[0] == '0' && lower((unsigned char)s[1]) == 'x' &&
        (digit(s[2]) >= 0 || (s[2] == '.' && digit(s[3]) >= 0))) { base = 16; s += 2; }
    small(&numerator, 0);
    while (*s) {
        int value = digit((unsigned char)*s);
        if (value >= 0 && value < base) {
            any = 1;
            if (point && fraction < LONG_MAX / 16) fraction++;
            if (value || kept) {
                if (kept < 800) { multiply(&numerator, (unsigned)base, (unsigned)value); kept++; }
                else { if (dropped < LONG_MAX / 16) dropped++; if (value) sticky = 1; }
            }
            s++;
        } else if (*s == '.' && !point) { point = 1; s++; }
        else break;
    }
    if (!any) return 0.0;
    power = exponent_part(&s, base == 16 ? 'p' : 'e');
    if (endp) *endp = (char *)s;
    if (!kept) return from_bits(negative ? 0x8000000000000000UL : 0);
    small(&denominator, 1);
    if (base == 16) power += 4 * (dropped - fraction);
    else {
        power += dropped - fraction;
        long magnitude = power + kept - 1;
        if (magnitude > 309) return range_result(1, negative);
        if (magnitude < -325) return range_result(0, negative);
        if (power >= 0) for (long i = 0; i < power; ++i) multiply(&numerator, 5, 0);
        else for (long i = 0; i < -power; ++i) multiply(&denominator, 5, 0);
    }
    return rational(&numerator, &denominator, power, sticky, negative);
}
