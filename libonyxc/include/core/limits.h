/*
 * limits.h — standard numeric limits (libonyxc v0.5).
 */
#ifndef _ONYX_LIMITS_H
#define _ONYX_LIMITS_H

#define CHAR_BIT    8
#define SCHAR_MIN   (-128)
#define SCHAR_MAX   127
#define UCHAR_MAX   255
#define CHAR_MIN    (-128)
#define CHAR_MAX    127

#define SHRT_MIN    (-32768)
#define SHRT_MAX    32767
#define USHRT_MAX   65535

#define INT_MIN     (-2147483647 - 1)
#define INT_MAX     2147483647
#define UINT_MAX    4294967295u

#define LONG_MIN    (-9223372036854775807L - 1L)
#define LONG_MAX    9223372036854775807L
#define ULONG_MAX   18446744073709551615UL

#define LLONG_MIN   (-9223372036854775807LL - 1LL)
#define LLONG_MAX   9223372036854775807LL
#define ULLONG_MAX  18446744073709551615ULL

#define MB_LEN_MAX  4

#define PATH_MAX    256     /* OnyxFS paths are currently 256 bytes */
#define NAME_MAX    255
#define FILENAME_MAX 256

#endif /* _ONYX_LIMITS_H */
