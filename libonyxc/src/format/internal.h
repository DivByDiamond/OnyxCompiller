#ifndef ONYX_FORMAT_INTERNAL_H
#define ONYX_FORMAT_INTERNAL_H
#define ONYX_FMT_MINUS 1
#define ONYX_FMT_PLUS 2
#define ONYX_FMT_SPACE 4
#define ONYX_FMT_HASH 8
#define ONYX_FMT_ZERO 16
int _onyx_format_float(double value, char spec, int precision, int width,
    unsigned flags, int (*emit)(void *, char), void *context);
#endif
