// EXPECT: 8 8 4 12
// EXPECT: 16 3 255 -1
// EXPECT: 7 9
// EXPECT: 1 1 1 1
// EXPECT: 1 1 1 1
#include <stdio.h>
#include <stddef.h>
struct inner { short value; };
struct payload { char tag; int values[3]; struct inner nested; };
static int array[3];
enum {
    pointer_size = sizeof(void *),
    string_size = sizeof("a\0b"),
    object_size = sizeof array,
    member_offset = offsetof(struct payload, values[2]),
    nested_offset = offsetof(struct payload, nested.value),
    selected = 0 ? 1 : 1 ? 3 : 4,
    byte_cast = (unsigned char)511,
    signed_cast = (signed char)255,
    grouped = (1 ? 7 : 8),
    nested = 1 ? (0 ? 1 : 9) : 2,
    bool_cast = (_Bool)256 == 1 && (_Bool)-4 == 1 && (_Bool)0 == 0,
    unsigned_compare = -1 > 1U && (unsigned long)-1 > 1L &&
                       (unsigned int)-1 < 4294967296L,
    signed_compare = (long)(unsigned int)-1 > 0 && (signed char)255 < 0,
    unsigned_arithmetic = ((unsigned int)-1 + 1U) == 0 &&
                          ((unsigned long)-1 / 2) == 9223372036854775807UL,
    safe_overflow = (2147483647 + 1) == (-2147483647 - 1) &&
                    (-9223372036854775807L - 1L) / -1L == (-9223372036854775807L - 1L),
    signed_shift = ((-4 >> 1) == -2) && ((1U << 31) > 0),
    conditional_type = (1 ? -1 : 1U) > 0,
    discarded = (0 && (1 / 0)) == 0 && (1 || (1 << 99)) == 1 &&
                (1 ? 7 : 1 / 0) == 7
};
struct buffer { char bytes[sizeof(void *)]; };
static char verify_offsets[member_offset == 12 && nested_offset == 16 ? 1 : -1];
int main(void) {
    struct buffer p;
    printf("%d %d %d %d\n", (int)sizeof(p), pointer_size, string_size, object_size);
    printf("%d %d %d %d\n", nested_offset, selected, byte_cast, signed_cast);
    printf("%d %d\n", grouped, nested);
    printf("%d %d %d %d\n", bool_cast, unsigned_compare, signed_compare, unsigned_arithmetic);
    printf("%d %d %d %d\n", safe_overflow, signed_shift, conditional_type, discarded);
    return 0;
}
