/* Exact finite binary64 -> decimal digits, rounded to nearest/ties-even.
 * 1100 decimal slots cover binary64's largest exact fractional expansion.
 * Output is streamed: requested precision cannot overflow a local buffer. */
#include <stdint.h>
#include "internal.h"

struct decimal { unsigned char digit[1100]; int length, scale; };
struct output { int (*emit)(void *, char); void *context; };
static int put(struct output *out, char ch) { return out->emit(out->context, ch); }
static int repeat(struct output *out, char ch, int count) {
    while (count-- > 0) if (put(out, ch) < 0) return -1;
    return 0;
}
static void multiply(struct decimal *d, int factor) {
    int i, carry = 0;
    for (i = 0; i < d->length; ++i) {
        int n = d->digit[i] * factor + carry;
        d->digit[i] = n % 10; carry = n / 10;
    }
    while (carry) { d->digit[d->length++] = carry % 10; carry /= 10; }
}
static void convert(struct decimal *d, uint64_t bits) {
    uint64_t mantissa = bits & 0xfffffffffffffUL;
    int exponent = (int)((bits >> 52) & 2047);
    int i;
    if (exponent) mantissa |= 0x10000000000000UL;
    exponent = exponent ? exponent - 1075 : -1074;
    d->length = d->scale = 0;
    do { d->digit[d->length++] = mantissa % 10; mantissa /= 10; } while (mantissa);
    if (exponent >= 0) for (i = 0; i < exponent; ++i) multiply(d, 2);
    else {
        d->scale = -exponent;
        for (i = 0; i < -exponent; ++i) multiply(d, 5);
    }
}
static int is_zero(struct decimal *d) { return d->length == 1 && !d->digit[0]; }
static int exponent10(struct decimal *d) { return is_zero(d) ? 0 : d->length - d->scale - 1; }
static int digit_at(struct decimal *d, long power) {
    long index = d->scale + power;
    return index >= 0 && index < d->length ? d->digit[index] : 0;
}
static void round_decimal(struct decimal *d, int cut) {
    int i, up = 0, sticky = 0;
    if (cut <= 0) return;
    if (cut <= d->length) {
        for (i = 0; i < cut - 1; ++i) if (d->digit[i]) sticky = 1;
        up = d->digit[cut - 1] > 5 || (d->digit[cut - 1] == 5 &&
            (sticky || (cut < d->length && (d->digit[cut] & 1))));
    }
    for (i = 0; i < cut && i < d->length; ++i) d->digit[i] = 0;
    if (up) {
        i = cut;
        while (i < d->length && d->digit[i] == 9) d->digit[i++] = 0;
        if (i == d->length) d->digit[d->length++] = 1;
        else d->digit[i]++;
    }
    while (d->length > 1 && !d->digit[d->length - 1]) d->length--;
}
static int fractional_digits(struct decimal *d, int scientific, int exponent, int count) {
    int lowest = 0, needed;
    if (is_zero(d)) return 0;
    while (lowest < d->length && !d->digit[lowest]) lowest++;
    needed = scientific ? exponent + d->scale - lowest : d->scale - lowest;
    if (needed < 0) needed = 0;
    return count < needed ? count : needed;
}
static int body(struct output *out, struct decimal *d, int scientific,
                int exponent, int fraction, int hash, int upper) {
    long power;
    int e, divisor;
    if (scientific) {
        if (put(out, (char)('0' + digit_at(d, exponent))) < 0) return -1;
    } else {
        power = exponent > 0 ? exponent : 0;
        for (; power >= 0; --power)
            if (put(out, (char)('0' + digit_at(d, power))) < 0) return -1;
    }
    if (fraction || hash) if (put(out, '.') < 0) return -1;
    power = scientific ? exponent - 1 : -1;
    for (int i = 0; i < fraction; ++i, --power)
        if (put(out, (char)('0' + digit_at(d, power))) < 0) return -1;
    if (!scientific) return 0;
    if (put(out, upper ? 'E' : 'e') < 0 || put(out, exponent < 0 ? '-' : '+') < 0) return -1;
    e = exponent < 0 ? -exponent : exponent;
    divisor = e >= 100 ? 100 : 10;
    for (; divisor; divisor /= 10)
        if (put(out, (char)('0' + e / divisor % 10)) < 0) return -1;
    return 0;
}
int _onyx_format_float(double value, char spec, int precision, int width,
                      unsigned flags, int (*emit)(void *, char), void *context) {
    union { double value; uint64_t bits; } number;
    struct decimal d;
    struct output out;
    int sign, scientific, general, upper, exponent, fraction, padding;
    long length;
    char sign_char = 0;
    const char *special = 0;
    number.value = value;
    out.emit = emit; out.context = context;
    sign = (int)(number.bits >> 63);
    upper = spec >= 'A' && spec <= 'Z';
    general = spec == 'g' || spec == 'G';
    scientific = spec == 'e' || spec == 'E';
    if (sign) sign_char = '-';
    else if (flags & ONYX_FMT_PLUS) sign_char = '+';
    else if (flags & ONYX_FMT_SPACE) sign_char = ' ';
    if (((number.bits >> 52) & 2047) == 2047) {
        special = (number.bits & 0xfffffffffffffUL) ? (upper ? "NAN" : "nan") : (upper ? "INF" : "inf");
        length = 3;
        flags &= ~ONYX_FMT_ZERO;
        exponent = fraction = 0;
    } else {
        convert(&d, number.bits);
        if (precision < 0) precision = 6;
        if (general && !precision) precision = 1;
        if (general || scientific) {
            int significant = general ? precision : (precision < 2147483647 ? precision + 1 : precision);
            round_decimal(&d, d.length - significant);
        } else round_decimal(&d, d.scale - precision);
        exponent = exponent10(&d);
        if (general) scientific = exponent < -4 || exponent >= precision;
        if (general) {
            long n = scientific ? (long)precision - 1 : (long)precision - exponent - 1;
            fraction = n > 2147483647 ? 2147483647 : (n > 0 ? (int)n : 0);
            if (!(flags & ONYX_FMT_HASH)) fraction = fractional_digits(&d, scientific, exponent, fraction);
        } else fraction = precision;
        length = scientific ? 1 : (exponent >= 0 ? exponent + 1 : 1);
        length += fraction + (long)(fraction > 0 || (flags & ONYX_FMT_HASH));
        if (scientific) length += (exponent >= 100 || exponent <= -100) ? 5 : 4;
    }
    if (sign_char) length++;
    padding = width > length ? (int)(width - length) : 0;
    if (!(flags & (ONYX_FMT_MINUS | ONYX_FMT_ZERO))) if (repeat(&out, ' ', padding) < 0) return -1;
    if (sign_char && put(&out, sign_char) < 0) return -1;
    if ((flags & ONYX_FMT_ZERO) && !(flags & ONYX_FMT_MINUS)) if (repeat(&out, '0', padding) < 0) return -1;
    if (special) {
        for (int i = 0; i < 3; ++i) if (put(&out, special[i]) < 0) return -1;
    } else if (body(&out, &d, scientific, exponent, fraction, flags & ONYX_FMT_HASH, upper) < 0) return -1;
    if (flags & ONYX_FMT_MINUS) if (repeat(&out, ' ', padding) < 0) return -1;
    return 0;
}
