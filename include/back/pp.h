/*
 * pp.h — preprocessor.
 *
 * Handles: #include (search paths), #define (object-like and function-like
 * without varargs for MVP), #undef, #ifdef/#ifndef/#if/#elif/#else/#endif
 * (with defined() and basic constant-expression evaluation),
 * #pragma once, #line.
 *
 * Does NOT do token-pasting (##) or stringification (#) yet — minimal
 * but enough for stdio.h-style headers.
 */
#ifndef CC_PP_H
#define CC_PP_H

#include "core/cc.h"

/* Reads file `path` from disk and returns its contents; *out_len gets the
 * size. Returns NULL on error. */
char *pp_read_file(const char *path, size_t *out_len);

/* Preprocess a source file into a single buffer. Returns malloc'd buffer
 * (caller frees). */
char *pp_preprocess_file(const char *path,
                         const char *const *include_paths, int n_include_paths,
                         const char *const *define_macros, int n_define_macros,
                         size_t *out_len);

long pp_eval_integer(const char *expression);

#endif /* CC_PP_H */
