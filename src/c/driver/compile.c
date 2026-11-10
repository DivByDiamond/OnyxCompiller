/*
 * driver/compile.c — per-file language mode and `-c` object compilation.
 *
 * `.cpp`/`.cc`/`.cxx` switch the parser into C++ mode for that file only
 * (multi-file builds mix C and C++). `-c` compiles exactly one translation
 * unit into a relocatable ONYO object: libonyxc is not auto-linked and
 * gen_finalize() is never called.
 */
#include "core/compat.h"

#include "core/cc.h"
#include "back/pp.h"
#include "front/lexer.h"
#include "front/ast.h"
#include "front/parse.h"
#include "back/gen.h"
#include "back/obj.h"
#include "back/emit.h"
#include "front/parser_priv.h"
#include "front/driver.h"

bool driver_is_cpp_source(const char *path) {
    size_t n = strlen(path);
    if (n >= 4 && strcmp(path + n - 4, ".cpp") == 0) return true;
    if (n >= 3 && strcmp(path + n - 3, ".cc") == 0) return true;
    if (n >= 4 && strcmp(path + n - 4, ".cxx") == 0) return true;
    return false;
}

int driver_compile_object(const char *input, const char *output) {
    g_cpp_mode = driver_is_cpp_source(input);
    if (g_opts.verbose)
        fprintf(stderr, "onyxcc: compiling %s -> %s\n", input, output);
    size_t pp_len = 0;
    char *pp_src = pp_preprocess_file(input,
                                      g_opts.include_paths, g_opts.n_include_paths,
                                      g_opts.define_macros, g_opts.n_define_macros,
                                      &pp_len);
    if (!pp_src) return 1;
    lexer_t lx;
    lex_init(&lx, pp_src, pp_len, input);
    parse_translation_unit(&lx);
    free(pp_src);
    if (cc_get_errors() > 0) return 1;
    return obj_emit(output);
}
